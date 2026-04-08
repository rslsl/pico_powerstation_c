#include <Arduino.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include <vector>

#include "pico_ota_ui.h"
#include "web_ui.h"

#ifndef PSTATION_UART_RX_PIN
#define PSTATION_UART_RX_PIN 16
#endif

#ifndef PSTATION_UART_TX_PIN
#define PSTATION_UART_TX_PIN 17
#endif

#ifndef PSTATION_UART_BAUD
#define PSTATION_UART_BAUD 115200
#endif

namespace {

constexpr size_t kUartLineMax = 1100;
constexpr uint32_t kHeartbeatMs = 5000;
constexpr uint32_t kHelloMs = 2000;
constexpr uint32_t kStatsRefreshMs = 15000;
constexpr uint32_t kSettingsRefreshMs = 30000;
constexpr uint32_t kTelemetryRefreshMs = 4000;
constexpr uint32_t kTimeSyncMs = 60000;   // send SET time to Pico every 60s
constexpr uint32_t kOtaRefreshMs = 4000;
constexpr uint32_t kLinkTimeoutMs = 15000;
constexpr uint32_t kRequestTimeoutMs = 1800;
constexpr uint32_t kPicoOtaBeginTimeoutMs = 30000;
constexpr uint32_t kPicoOtaChunkTimeoutMs = 10000;
constexpr uint32_t kPicoOtaFinishTimeoutMs = 20000;
constexpr uint32_t kOtaStepMs = 600;
constexpr uint32_t kLogBatchSize = 8;
constexpr size_t kPicoOtaChunkBytes = 48;
constexpr char kPicoOtaStagePath[] = "/pico_slot.bin";
constexpr uint32_t kPicoOtaStageMaxBytes = 1024u * 1024u;

enum class PicoTransferStep : uint8_t {
  Idle = 0,
  Staging,
  Queued,
  BeginWait,
  ChunkPrepare,
  ChunkWait,
  EndWait,
  Done,
  Failed,
};

enum class AckPollResult : uint8_t {
  Pending = 0,
  Ok,
  Fail,
};

enum class BridgeMode : uint8_t {
  Off = 0,
  Web = 1,
  Ota = 2,
  PicoOta = 3,
};

enum class WifiProfileMode : uint8_t {
  Ap = 0,
  Sta = 1,
  ApSta = 2,
};

struct NetworkConfig {
  WifiProfileMode wifiMode = WifiProfileMode::Ap;
  String staSsid;
  String staPassword;
  String apSsid;
  String apPassword;
  String hostname;
  String otaPassword;
  bool ntpEnabled = true;
  String ntpServer = "pool.ntp.org";
  int32_t tzOffsetSec = 7200; // UTC+2 (Kyiv winter)
  String adminPassword;       // empty = no auth
};

struct BridgeCache {
  String helloJson;
  String telemetryJson;
  String statsJson;
  String settingsJson;
  String otaJson;
  String lastAckJson;
  String lastErrorJson;
  String portsJson;
  String rpMode = "OFF";

  bool linkUp = false;
  uint32_t helloCounter = 0;
  uint32_t telemetryCounter = 0;
  uint32_t statsCounter = 0;
  uint32_t settingsCounter = 0;
  uint32_t portsCounter = 0;
  uint32_t otaCounter = 0;
  uint32_t ackCounter = 0;
  uint32_t errorCounter = 0;

  uint32_t logTotal = 0;
  uint32_t logStart = 0;
  uint32_t logExpected = 0;
  bool logPending = false;
  bool logMetaReady = false;
  std::vector<String> logEvents;

  unsigned long lastRxMs = 0;
  unsigned long lastHelloMs = 0;
  unsigned long lastTelemetryMs = 0;
  unsigned long lastStatsMs = 0;
  unsigned long lastSettingsMs = 0;
  unsigned long lastOtaMs = 0;

  // CLI capture
  bool cliCapturing = false;
  std::vector<String> cliLines;
};

struct PicoUploadState {
  bool active = false;
  bool failed = false;
  PicoTransferStep step = PicoTransferStep::Idle;
  uint32_t totalBytes = 0;
  uint32_t stagedBytes = 0;
  uint32_t sentBytes = 0;
  uint32_t crc32 = 0;
  bool requestOpen = false;
  uint32_t requestOffset = 0;
  uint32_t requestExpectedBytes = 0;
  uint32_t requestReceivedBytes = 0;
  uint32_t pendingChunkOffset = 0;
  uint32_t pendingChunkLen = 0;
  uint32_t pendingChunkCrc32 = 0;
  uint32_t waitAckCounter = 0;
  uint32_t waitErrorCounter = 0;
  unsigned long waitDeadlineMs = 0;
  const char *waitExpectedCmd = nullptr;
  const char *waitContext = nullptr;
  unsigned long lastProgressMs = 0;
  String waitLastIgnoredAck;
  String versionTag;
  String lastMessage;
};

Preferences gPrefs;
WebServer gServer(80);
DNSServer gDns;
HardwareSerial gBridge(1);
NetworkConfig gNetCfg;
BridgeCache gCache;
PicoUploadState gPicoUpload;
File gPicoStageFile;
bool gSpiffsReady = false;
char gUartLine[kUartLineMax];
size_t gUartLen = 0;

BridgeMode gBridgeMode = BridgeMode::Web;
String gBridgeStatus = "ESP BOOT";
String gLastStatusSent;

unsigned long gLastHeartbeatMs = 0;
unsigned long gLastHelloReqMs = 0;
unsigned long gLastStatsReqMs = 0;
unsigned long gLastSettingsReqMs = 0;
unsigned long gLastTelemetryReqMs = 0;
unsigned long gLastOtaReqMs = 0;
unsigned long gLastOtaStatusMs = 0;
unsigned long gLastTimeSyncMs = 0;
bool gNtpSynced = false;
time_t gNtpEpoch = 0;
unsigned long gNtpSyncMs = 0;

}  // namespace

static void pollBridgeRx();
static String jsonValue(const String &json, const char *key);
static bool jsonBoolValue(const String &json, const char *key);
static void abortPicoOta(const String &reason);
static void setBridgeStatusLocal(const String &status);
static void sendJsonError(int code, const String &message);
static bool parseUint32Arg(const String &text, uint32_t *valueOut);
static bool appendStagedPicoChunk(const uint8_t *data, size_t len, uint32_t offset, String *messageOut);
static bool startStagedPicoUpload(uint32_t imageSize, const String &versionTag, String *messageOut);
static bool commitStagedPicoUpload(String *messageOut);

static const char *picoUploadPhaseName() {
  switch (gPicoUpload.step) {
    case PicoTransferStep::Staging: return "staging";
    case PicoTransferStep::Queued: return "queued";
    case PicoTransferStep::BeginWait:
    case PicoTransferStep::ChunkPrepare:
    case PicoTransferStep::ChunkWait: return "transferring";
    case PicoTransferStep::EndWait: return "finalizing";
    case PicoTransferStep::Done: return "done";
    case PicoTransferStep::Failed: return "failed";
    case PicoTransferStep::Idle:
    default: break;
  }
  return "idle";
}

static bool picoOtaUiModeActive() {
  if (gPicoUpload.active) return true;
  if (gCache.otaJson.isEmpty()) return gBridgeMode == BridgeMode::PicoOta;
  return jsonBoolValue(gCache.otaJson, "safe_ready");
}

static void resetPicoUploadRequest() {
  gPicoUpload.requestOpen = false;
  gPicoUpload.requestOffset = 0u;
  gPicoUpload.requestExpectedBytes = 0u;
  gPicoUpload.requestReceivedBytes = 0u;
}

static void closePicoStageFile() {
  if (gPicoStageFile) gPicoStageFile.close();
}

static void clearPicoStageFile() {
  closePicoStageFile();
  if (gSpiffsReady) SPIFFS.remove(kPicoOtaStagePath);
}

static bool parseUint32Arg(const String &text, uint32_t *valueOut) {
  char *end = nullptr;
  unsigned long value = 0ul;
  if (!valueOut || text.isEmpty()) return false;
  value = strtoul(text.c_str(), &end, 0);
  if (!end || *end != '\0' || value > 0xFFFFFFFFul) return false;
  *valueOut = static_cast<uint32_t>(value);
  return true;
}

static void failPicoUpload(const String &message, bool sendAbort = false) {
  if (sendAbort) abortPicoOta("bridge_failed");
  clearPicoStageFile();
  resetPicoUploadRequest();
  gPicoUpload.active = false;
  gPicoUpload.failed = true;
  gPicoUpload.step = PicoTransferStep::Failed;
  gPicoUpload.waitExpectedCmd = nullptr;
  gPicoUpload.waitContext = nullptr;
  gPicoUpload.waitDeadlineMs = 0;
  gPicoUpload.lastMessage = message;
  setBridgeStatusLocal("PICO OTA FAIL");
}

static void startPicoAckWait(const char *expectedCmd, const char *context, uint32_t timeoutMs) {
  gPicoUpload.waitExpectedCmd = expectedCmd;
  gPicoUpload.waitContext = context;
  gPicoUpload.waitAckCounter = gCache.ackCounter;
  gPicoUpload.waitErrorCounter = gCache.errorCounter;
  gPicoUpload.waitDeadlineMs = millis() + timeoutMs;
  gPicoUpload.waitLastIgnoredAck = "";
}

static AckPollResult pollPicoAckWait(String *messageOut) {
  if (gCache.errorCounter != gPicoUpload.waitErrorCounter) {
    if (messageOut) *messageOut = gCache.lastErrorJson;
    return AckPollResult::Fail;
  }

  if (gCache.ackCounter != gPicoUpload.waitAckCounter) {
    const String ackCmd = jsonValue(gCache.lastAckJson, "cmd");
    gPicoUpload.waitAckCounter = gCache.ackCounter;
    if (gPicoUpload.waitExpectedCmd && *gPicoUpload.waitExpectedCmd &&
        !ackCmd.equalsIgnoreCase(gPicoUpload.waitExpectedCmd)) {
      gPicoUpload.waitLastIgnoredAck = ackCmd;
      return AckPollResult::Pending;
    }
    if (messageOut) *messageOut = gCache.lastAckJson;
    return jsonBoolValue(gCache.lastAckJson, "ok") ? AckPollResult::Ok : AckPollResult::Fail;
  }

  if (static_cast<int32_t>(gPicoUpload.waitDeadlineMs - millis()) > 0) {
    return AckPollResult::Pending;
  }

  if (messageOut) {
    String timeoutMessage = "timeout waiting for RP2040 ack";
    if (gPicoUpload.waitContext && *gPicoUpload.waitContext) {
      timeoutMessage += " during ";
      timeoutMessage += gPicoUpload.waitContext;
    }
    if (!gPicoUpload.waitLastIgnoredAck.isEmpty()) {
      timeoutMessage += " (last unrelated ack=";
      timeoutMessage += gPicoUpload.waitLastIgnoredAck;
      timeoutMessage += ")";
    }
    *messageOut = timeoutMessage;
  }
  return AckPollResult::Fail;
}

static bool rejectIfGeneralUiLocked() {
  if (!picoOtaUiModeActive()) return false;
  sendJsonError(423, "General UI is disabled while Pico OTA mode is active");
  return true;
}

static String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char ch = value[i];
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += ch; break;
    }
  }
  return out;
}

static String csvEscape(const String &value) {
  String out = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    if (value[i] == '"') out += "\"\"";
    else out += value[i];
  }
  out += "\"";
  return out;
}

static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len) {
  crc ^= 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1u))));
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

static String sanitizeVersionTag(const String &filename) {
  String base = filename;
  const int slash = max(base.lastIndexOf('/'), base.lastIndexOf('\\'));
  if (slash >= 0) base = base.substring(slash + 1);
  const int dot = base.lastIndexOf('.');
  if (dot > 0) base = base.substring(0, dot);
  base.trim();
  if (base.isEmpty()) base = "web-ota";

  String out;
  out.reserve(24);
  for (size_t i = 0; i < base.length() && out.length() < 23; ++i) {
    const char ch = base[i];
    if (isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') out += ch;
    else out += '_';
  }
  if (out.isEmpty()) out = "web-ota";
  return out;
}

static String bytesToHex(const uint8_t *data, size_t len) {
  static const char kHex[] = "0123456789ABCDEF";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += kHex[(data[i] >> 4) & 0x0F];
    out += kHex[data[i] & 0x0F];
  }
  return out;
}

static String jsonValue(const String &json, const char *key) {
  const String needle = String("\"") + key + "\":";
  int pos = json.indexOf(needle);
  if (pos < 0) return "";
  pos += needle.length();
  while (pos < json.length() && isspace(static_cast<unsigned char>(json[pos]))) ++pos;
  if (pos >= json.length()) return "";

  if (json[pos] == '"') {
    ++pos;
    String out;
    while (pos < json.length()) {
      const char ch = json[pos++];
      if (ch == '\\' && pos < json.length()) {
        out += json[pos++];
        continue;
      }
      if (ch == '"') break;
      out += ch;
    }
    return out;
  }

  int end = pos;
  while (end < json.length() && json[end] != ',' && json[end] != '}') ++end;
  String out = json.substring(pos, end);
  out.trim();
  return out;
}

static bool jsonBoolValue(const String &json, const char *key) {
  const String value = jsonValue(json, key);
  return value == "1" || value == "true" || value == "TRUE";
}

static String deviceSuffix() {
  const uint64_t mac = ESP.getEfuseMac();
  char buf[7];
  snprintf(buf, sizeof(buf), "%06llX", static_cast<unsigned long long>(mac & 0xFFFFFFULL));
  return String(buf);
}

static const char *bridgeModeName(BridgeMode mode) {
  switch (mode) {
    case BridgeMode::PicoOta: return "PICO OTA";
    case BridgeMode::Web: return "WEB";
    case BridgeMode::Ota: return "OTA";
    case BridgeMode::Off:
    default: return "OFF";
  }
}

static BridgeMode bridgeModeFromText(const String &text) {
  if (text.equalsIgnoreCase("OTA")) return BridgeMode::Ota;
  if (text.equalsIgnoreCase("WEB")) return BridgeMode::Web;
  return BridgeMode::Off;
}

static bool picoUploadSessionPinned() {
  return gPicoUpload.active;
}

static bool bridgeCacheWantsPicoOtaMode() {
  if (picoUploadSessionPinned()) return true;
  if (gCache.otaJson.isEmpty()) return false;
  if (!jsonBoolValue(gCache.otaJson, "safe_ready")) return false;
  return (millis() - gCache.lastOtaMs) < (kOtaRefreshMs * 2u);
}

static const char *wifiProfileModeName(WifiProfileMode mode) {
  switch (mode) {
    case WifiProfileMode::Sta: return "sta";
    case WifiProfileMode::ApSta: return "ap_sta";
    case WifiProfileMode::Ap:
    default: return "ap";
  }
}

static WifiProfileMode wifiProfileModeFromText(const String &text) {
  if (text.equalsIgnoreCase("sta")) return WifiProfileMode::Sta;
  if (text.equalsIgnoreCase("ap_sta")) return WifiProfileMode::ApSta;
  return WifiProfileMode::Ap;
}

static String defaultApSsid() {
  return String("PowerStation-") + deviceSuffix();
}

static String defaultHostname() {
  return String("powerstation-") + deviceSuffix();
}

static String ipOrEmpty(IPAddress ip) {
  return ip == IPAddress(0, 0, 0, 0) ? String("") : ip.toString();
}

static void sendBridgeLine(const String &line) {
  String payload = line;
  payload += "\r\n";
  gBridge.write(reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length());
}

static void sendBridgeBuffer(const char *data, size_t len) {
  if (!data || len == 0u) return;
  gBridge.write(reinterpret_cast<const uint8_t *>(data), len);
  gBridge.write(reinterpret_cast<const uint8_t *>("\r\n"), 2u);
}

static void setBridgeStatusLocal(const String &status) {
  gBridgeStatus = status.substring(0, 23);
}

static void sendBridgeStatus(const String &status) {
  if (gPicoUpload.active) {
    setBridgeStatusLocal(status);
    return;
  }
  const String clipped = status.substring(0, 23);
  gBridgeStatus = clipped;
  if (clipped == gLastStatusSent) return;
  sendBridgeLine(String("STATUS ") + clipped);
  gLastStatusSent = clipped;
}

static bool bridgeRequestsAllowed() {
  return !gPicoUpload.active;
}

static void quiesceBridgeForPicoOtaStart() {
  const unsigned long now = millis();
  gLastHeartbeatMs = now;
  gLastHelloReqMs = now;
  gLastStatsReqMs = now;
  gLastSettingsReqMs = now;
  gLastTelemetryReqMs = now;
  gBridge.flush();
  delay(40);
  pollBridgeRx();
}

static void loadNetworkConfig() {
  gNetCfg.wifiMode = static_cast<WifiProfileMode>(gPrefs.getUChar("wifi_mode", static_cast<uint8_t>(WifiProfileMode::ApSta)));
  gNetCfg.staSsid = gPrefs.getString("sta_ssid", "Pixel");
  gNetCfg.staPassword = gPrefs.getString("sta_pass", "01111993");
  gNetCfg.apSsid = gPrefs.getString("ap_ssid", defaultApSsid());
  gNetCfg.apPassword = gPrefs.getString("ap_pass", "powerstat");
  gNetCfg.hostname = gPrefs.getString("host", defaultHostname());
  gNetCfg.otaPassword = gPrefs.getString("ota_pass", "");

  gNetCfg.ntpEnabled = gPrefs.getBool("ntp_en", true);
  gNetCfg.ntpServer = gPrefs.getString("ntp_srv", "pool.ntp.org");
  gNetCfg.tzOffsetSec = gPrefs.getInt("tz_off", 7200);
  gNetCfg.adminPassword = gPrefs.getString("admin_pw", "");

  if (gNetCfg.apSsid.isEmpty()) gNetCfg.apSsid = defaultApSsid();
  if (gNetCfg.apPassword.length() < 8) gNetCfg.apPassword = "powerstat";
  if (gNetCfg.hostname.isEmpty()) gNetCfg.hostname = defaultHostname();
  if (gNetCfg.ntpServer.isEmpty()) gNetCfg.ntpServer = "pool.ntp.org";
  if (static_cast<uint8_t>(gNetCfg.wifiMode) > static_cast<uint8_t>(WifiProfileMode::ApSta)) {
    gNetCfg.wifiMode = WifiProfileMode::Ap;
  }
}

static void saveNetworkConfig() {
  gPrefs.putUChar("wifi_mode", static_cast<uint8_t>(gNetCfg.wifiMode));
  gPrefs.putString("sta_ssid", gNetCfg.staSsid);
  gPrefs.putString("sta_pass", gNetCfg.staPassword);
  gPrefs.putString("ap_ssid", gNetCfg.apSsid);
  gPrefs.putString("ap_pass", gNetCfg.apPassword);
  gPrefs.putString("host", gNetCfg.hostname);
  gPrefs.putString("ota_pass", gNetCfg.otaPassword);
  gPrefs.putBool("ntp_en", gNetCfg.ntpEnabled);
  gPrefs.putString("ntp_srv", gNetCfg.ntpServer);
  gPrefs.putInt("tz_off", gNetCfg.tzOffsetSec);
  gPrefs.putString("admin_pw", gNetCfg.adminPassword);
}

static void announceModeStatus() {
  if (gBridgeMode == BridgeMode::PicoOta) sendBridgeStatus("PICO OTA READY");
  else if (gBridgeMode == BridgeMode::Ota) sendBridgeStatus("OTA READY");
  else if (gBridgeMode == BridgeMode::Web) sendBridgeStatus("WEB READY");
  else sendBridgeStatus("BRIDGE IDLE");
}

static void refreshBridgeMode() {
  const BridgeMode next = bridgeCacheWantsPicoOtaMode() ? BridgeMode::PicoOta : bridgeModeFromText(gCache.rpMode);
  if (next != gBridgeMode) {
    gBridgeMode = next;
    announceModeStatus();
  } else if (gBridgeStatus == "ESP BOOT" || gBridgeStatus == "WEB WAIT" ||
             gBridgeStatus == "OTA WAIT" || gBridgeStatus == "PICO OTA WAIT") {
    announceModeStatus();
  }
}

static void applyBridgeModeFromRp(const String &modeText) {
  if (!modeText.isEmpty()) gCache.rpMode = modeText;
  refreshBridgeMode();
}

static void configureArduinoOta() {
  ArduinoOTA.setHostname(gNetCfg.hostname.c_str());
  if (!gNetCfg.otaPassword.isEmpty()) {
    ArduinoOTA.setPassword(gNetCfg.otaPassword.c_str());
  }

  ArduinoOTA.onStart([]() {
    gLastOtaStatusMs = 0;
    sendBridgeStatus("OTA START");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    const unsigned long now = millis();
    if (total == 0u || (now - gLastOtaStatusMs) < kOtaStepMs) return;
    const unsigned long pct = (progress * 100u) / total;
    sendBridgeStatus(String("OTA ") + pct + "%");
    gLastOtaStatusMs = now;
  });
  ArduinoOTA.onEnd([]() {
    sendBridgeStatus("OTA OK");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    sendBridgeStatus(String("OTA ERR ") + static_cast<int>(error));
  });
  ArduinoOTA.begin();
}

static void applyNetworkConfig() {
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(80);

  const bool wantAp = gNetCfg.wifiMode == WifiProfileMode::Ap || gNetCfg.wifiMode == WifiProfileMode::ApSta;
  const bool wantSta = (gNetCfg.wifiMode == WifiProfileMode::Sta || gNetCfg.wifiMode == WifiProfileMode::ApSta) &&
                       !gNetCfg.staSsid.isEmpty();

  if (wantAp && wantSta) WiFi.mode(WIFI_AP_STA);
  else if (wantSta) WiFi.mode(WIFI_STA);
  else WiFi.mode(WIFI_AP);

  WiFi.setSleep(false);
  WiFi.setHostname(gNetCfg.hostname.c_str());

  if (wantAp) {
    WiFi.softAP(gNetCfg.apSsid.c_str(), gNetCfg.apPassword.c_str());
  }
  if (wantSta) {
    WiFi.begin(gNetCfg.staSsid.c_str(), gNetCfg.staPassword.c_str());
  }

  configureArduinoOta();

  // NTP time sync
  if (gNetCfg.ntpEnabled && wantSta) {
    configTime(gNetCfg.tzOffsetSec, 3600, gNetCfg.ntpServer.c_str(), "time.google.com");
    Serial.println("NTP: " + gNetCfg.ntpServer + " tz=" + String(gNetCfg.tzOffsetSec));
  }
}

static void processBridgeJson(const String &line) {
  const String type = jsonValue(line, "type");
  const unsigned long now = millis();

  gCache.linkUp = true;
  gCache.lastRxMs = now;

  if (type == "hello") {
    gCache.helloJson = line;
    gCache.helloCounter++;
    gCache.lastHelloMs = now;
    applyBridgeModeFromRp(jsonValue(line, "mode"));
  } else if (type == "telemetry") {
    gCache.telemetryJson = line;
    gCache.telemetryCounter++;
    gCache.lastTelemetryMs = now;
    applyBridgeModeFromRp(jsonValue(line, "mode"));
  } else if (type == "stats") {
    gCache.statsJson = line;
    gCache.statsCounter++;
    gCache.lastStatsMs = now;
  } else if (type == "settings") {
    gCache.settingsJson = line;
    gCache.settingsCounter++;
    gCache.lastSettingsMs = now;
    applyBridgeModeFromRp(jsonValue(line, "esp_mode"));
  } else if (type == "ports") {
    gCache.portsJson = line;
    gCache.portsCounter++;
  } else if (type == "ota") {
    gCache.otaJson = line;
    gCache.otaCounter++;
    gCache.lastOtaMs = now;
    refreshBridgeMode();
  } else if (type == "log_meta") {
    gCache.logTotal = static_cast<uint32_t>(jsonValue(line, "total").toInt());
    gCache.logStart = static_cast<uint32_t>(jsonValue(line, "start").toInt());
    gCache.logExpected = static_cast<uint32_t>(jsonValue(line, "sent").toInt());
    gCache.logEvents.clear();
    gCache.logMetaReady = true;
    gCache.logPending = gCache.logExpected != 0u;
  } else if (type == "log_event") {
    gCache.logEvents.push_back(line);
    if (gCache.logMetaReady && gCache.logEvents.size() >= gCache.logExpected) {
      gCache.logPending = false;
    }
  } else if (type == "ack") {
    gCache.lastAckJson = line;
    gCache.ackCounter++;
  } else if (type == "error") {
    gCache.lastErrorJson = line;
    gCache.errorCounter++;
  }
}

static void processBridgeLine(const String &line) {
  if (gCache.cliCapturing && gCache.cliLines.size() < 64u) {
    gCache.cliLines.push_back(line);
  }
  if (!line.isEmpty() && line[0] == '{') {
    processBridgeJson(line);
  }
}

static void pollBridgeRx() {
  while (gBridge.available()) {
    const char ch = static_cast<char>(gBridge.read());
    if (ch == '\r' || ch == '\n') {
      if (gUartLen > 0u) {
        gUartLine[gUartLen] = '\0';
        processBridgeLine(String(gUartLine));
        gUartLen = 0u;
      }
      continue;
    }
    if (static_cast<unsigned char>(ch) < 32u || static_cast<unsigned char>(ch) > 126u) continue;
    if (gUartLen + 1u >= kUartLineMax) {
      gUartLen = 0u;
      continue;
    }
    gUartLine[gUartLen++] = ch;
  }
}

static void serviceBackground() {
  pollBridgeRx();
  if (gBridgeMode == BridgeMode::Ota) ArduinoOTA.handle();
  delay(4);
}

static bool waitForCounter(uint32_t *counter, uint32_t before, uint32_t timeoutMs) {
  const unsigned long deadline = millis() + timeoutMs;
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    serviceBackground();
    if (*counter != before) return true;
  }
  return false;
}

static bool requestStatsIfNeeded(bool force) {
  const unsigned long now = millis();
  if (!force && !gCache.statsJson.isEmpty() && (now - gCache.lastStatsMs) < kStatsRefreshMs) return true;
  if (!bridgeRequestsAllowed()) return !gCache.statsJson.isEmpty();
  const uint32_t before = gCache.statsCounter;
  sendBridgeLine("GET STATS");
  gLastStatsReqMs = now;
  return waitForCounter(&gCache.statsCounter, before, kRequestTimeoutMs);
}

static bool requestSettingsIfNeeded(bool force) {
  const unsigned long now = millis();
  if (!force && !gCache.settingsJson.isEmpty() && (now - gCache.lastSettingsMs) < kSettingsRefreshMs) return true;
  if (!bridgeRequestsAllowed()) return !gCache.settingsJson.isEmpty();
  const uint32_t before = gCache.settingsCounter;
  sendBridgeLine("GET SETTINGS");
  gLastSettingsReqMs = now;
  return waitForCounter(&gCache.settingsCounter, before, kRequestTimeoutMs);
}

static bool requestTelemetryIfNeeded(bool force) {
  const unsigned long now = millis();
  if (!force && !gCache.telemetryJson.isEmpty() && (now - gCache.lastTelemetryMs) < kTelemetryRefreshMs) return true;
  if (!bridgeRequestsAllowed()) return !gCache.telemetryJson.isEmpty();
  const uint32_t before = gCache.telemetryCounter;
  sendBridgeLine("GET TELEMETRY");
  gLastTelemetryReqMs = now;
  return waitForCounter(&gCache.telemetryCounter, before, kRequestTimeoutMs);
}

static bool requestOtaIfNeeded(bool force) {
  const unsigned long now = millis();
  if (!force && !gCache.otaJson.isEmpty() && (now - gCache.lastOtaMs) < 1500u) return true;
  if (!bridgeRequestsAllowed()) return !gCache.otaJson.isEmpty();
  const uint32_t before = gCache.otaCounter;
  sendBridgeLine("GET OTA");
  gLastOtaReqMs = now;
  return waitForCounter(&gCache.otaCounter, before, kRequestTimeoutMs);
}

static bool preflightPicoOta(String *messageOut) {
  if (!gCache.linkUp) {
    if (messageOut) *messageOut = "RP2040 link is offline";
    return false;
  }
  if (!requestOtaIfNeeded(true) || gCache.otaJson.isEmpty()) {
    if (messageOut) *messageOut = "RP2040 OTA status unavailable";
    return false;
  }
  if (!jsonBoolValue(gCache.otaJson, "safe_ready")) {
    String message = "RP2040 is not in OTA SAFE mode";
    const String picoMode = jsonValue(gCache.otaJson, "pico_mode");
    if (!picoMode.isEmpty()) {
      message += " (";
      message += picoMode;
      message += ")";
    }
    if (messageOut) *messageOut = message;
    return false;
  }
  if (jsonBoolValue(gCache.otaJson, "reboot_pending")) {
    if (messageOut) *messageOut = "RP2040 is already rebooting after OTA";
    return false;
  }
  return true;
}

static bool startStagedPicoUpload(uint32_t imageSize, const String &versionTag, String *messageOut) {
  String message;

  if (gPicoUpload.active) {
    if (messageOut) *messageOut = "Another Pico OTA session is already active";
    return false;
  }
  if (imageSize == 0u || imageSize > kPicoOtaStageMaxBytes) {
    if (messageOut) *messageOut = imageSize == 0u ? "Upload size is zero" : "Pico image is too large for ESP staging";
    return false;
  }
  if (!preflightPicoOta(&message)) {
    if (messageOut) *messageOut = message;
    return false;
  }
  if (!gSpiffsReady) {
    if (messageOut) *messageOut = "ESP staging storage unavailable";
    return false;
  }

  gPicoUpload = PicoUploadState{};
  gPicoUpload.totalBytes = imageSize;
  gPicoUpload.versionTag = versionTag.isEmpty() ? String("web-ota") : versionTag;
  gPicoUpload.lastMessage = "Preparing Pico image staging on ESP";

  clearPicoStageFile();
  gPicoStageFile = SPIFFS.open(kPicoOtaStagePath, FILE_WRITE);
  if (!gPicoStageFile) {
    gPicoUpload.failed = true;
    gPicoUpload.lastMessage = "Failed to create staged Pico image";
    gPicoUpload.step = PicoTransferStep::Failed;
    setBridgeStatusLocal("PICO OTA FAIL");
    if (messageOut) *messageOut = gPicoUpload.lastMessage;
    return false;
  }

  gPicoUpload.active = true;
  gPicoUpload.step = PicoTransferStep::Staging;
  setBridgeStatusLocal("PICO OTA STAGE");
  if (messageOut) *messageOut = "ESP staging session ready";
  return true;
}

static bool appendStagedPicoChunk(const uint8_t *data, size_t len, uint32_t offset, String *messageOut) {
  if (!gPicoUpload.active || gPicoUpload.failed || gPicoUpload.step != PicoTransferStep::Staging) {
    if (messageOut) *messageOut = "Pico OTA staging session is not active";
    return false;
  }
  if (!data || len == 0u) {
    if (messageOut) *messageOut = "Chunk payload is empty";
    return false;
  }
  if (offset != gPicoUpload.stagedBytes) {
    if (messageOut) *messageOut = "Staged chunk offset mismatch";
    return false;
  }
  if ((static_cast<unsigned long long>(offset) + static_cast<unsigned long long>(len)) >
      static_cast<unsigned long long>(gPicoUpload.totalBytes)) {
    if (messageOut) *messageOut = "Staged chunk exceeds declared image size";
    return false;
  }
  if (!gPicoStageFile) {
    if (messageOut) *messageOut = "Staged Pico image handle is closed";
    return false;
  }

  const size_t written = gPicoStageFile.write(data, len);
  if (written != len) {
    if (messageOut) *messageOut = "Failed to write staged Pico image";
    return false;
  }

  gPicoUpload.stagedBytes += static_cast<uint32_t>(written);
  gPicoUpload.lastMessage = "Staging Pico image on ESP";
  const unsigned long now = millis();
  if ((now - gPicoUpload.lastProgressMs) >= kOtaStepMs && gPicoUpload.totalBytes > 0u) {
    const unsigned long pct =
        (static_cast<unsigned long long>(gPicoUpload.stagedBytes) * 100ull) / gPicoUpload.totalBytes;
    setBridgeStatusLocal(String("PICO RX ") + pct + "%");
    gPicoUpload.lastProgressMs = now;
  }
  if (messageOut) *messageOut = "Staged chunk";
  return true;
}

static bool commitStagedPicoUpload(String *messageOut) {
  if (!gPicoUpload.active || gPicoUpload.failed || gPicoUpload.step != PicoTransferStep::Staging) {
    if (messageOut) *messageOut = "Pico OTA staging session is not active";
    return false;
  }
  closePicoStageFile();
  if (gPicoUpload.stagedBytes != gPicoUpload.totalBytes) {
    if (messageOut) *messageOut = "Staged file size mismatch";
    return false;
  }
  gPicoUpload.step = PicoTransferStep::Queued;
  gPicoUpload.lastMessage = "Firmware staged on ESP. Starting transfer to RP2040";
  setBridgeStatusLocal("PICO OTA QUEUE");
  if (messageOut) *messageOut = gPicoUpload.lastMessage;
  return true;
}

static bool waitForAckOrError(const char *context, const char *expectedCmd, String *messageOut,
                              uint32_t timeoutMs = kRequestTimeoutMs) {
  const uint32_t ackBefore = gCache.ackCounter;
  const uint32_t errBefore = gCache.errorCounter;
  const unsigned long deadline = millis() + timeoutMs;
  String ignoredAck;

  while (static_cast<int32_t>(deadline - millis()) >= 0) {
    serviceBackground();
    if (gCache.errorCounter != errBefore) {
      if (messageOut) *messageOut = gCache.lastErrorJson;
      return false;
    }
    if (gCache.ackCounter != ackBefore) {
      const String ackCmd = jsonValue(gCache.lastAckJson, "cmd");
      if (expectedCmd && *expectedCmd && !ackCmd.equalsIgnoreCase(expectedCmd)) {
        ignoredAck = ackCmd;
        continue;
      }
      if (messageOut) *messageOut = gCache.lastAckJson;
      return jsonBoolValue(gCache.lastAckJson, "ok");
    }
  }

  if (messageOut) {
    String timeoutMessage = "timeout waiting for RP2040 ack";
    if (context && *context) {
      timeoutMessage += " during ";
      timeoutMessage += context;
    }
    timeoutMessage += " after ";
    timeoutMessage += timeoutMs;
    timeoutMessage += "ms";
    if (!ignoredAck.isEmpty()) {
      timeoutMessage += " (last unrelated ack=";
      timeoutMessage += ignoredAck;
      timeoutMessage += ")";
    }
    *messageOut = timeoutMessage;
  }
  return false;
}

static bool applyRpSetting(const String &key, const String &value, String *messageOut) {
  sendBridgeLine(String("SET ") + key + " " + value);
  return waitForAckOrError("set", "set", messageOut);
}

static bool applyRpAction(const String &action, String *messageOut) {
  sendBridgeLine(String("ACTION ") + action);
  return waitForAckOrError("action", "action", messageOut);
}

static bool bridgeWaitForAckWithCommand(const String &line, const char *context, const char *expectedCmd,
                                        String *messageOut,
                                        uint32_t timeoutMs = kRequestTimeoutMs) {
  sendBridgeLine(line);
  return waitForAckOrError(context, expectedCmd, messageOut, timeoutMs);
}

static bool beginPicoOta(uint32_t imageSize, const String &versionTag, String *messageOut) {
  char line[96];
  const String safeVersion = versionTag.isEmpty() ? String("web-ota") : versionTag;
  snprintf(line, sizeof(line), "OTA BEGIN %lu %s",
           static_cast<unsigned long>(imageSize), safeVersion.c_str());
  return bridgeWaitForAckWithCommand(String(line), "ota_begin", "ota_begin", messageOut, kPicoOtaBeginTimeoutMs);
}

static bool sendPicoOtaChunk(uint32_t offset, const uint8_t *data, size_t len, String *messageOut) {
  return bridgeWaitForAckWithCommand(
      String("OTA CHUNK ") + offset + " " + bytesToHex(data, len), "ota_chunk", "ota_chunk", messageOut,
      kPicoOtaChunkTimeoutMs);
}

static bool finishPicoOta(uint32_t crc32, String *messageOut) {
  char buf[16];
  snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(crc32));
  return bridgeWaitForAckWithCommand(String("OTA END ") + buf, "ota_end", "ota_end", messageOut, kPicoOtaFinishTimeoutMs);
}

static void abortPicoOta(const String &reason) {
  sendBridgeLine(String("OTA ABORT ") + reason);
}

static void beginPicoOtaAsync(uint32_t imageSize, const String &versionTag) {
  char line[96];
  const String safeVersion = versionTag.isEmpty() ? String("web-ota") : versionTag;
  snprintf(line, sizeof(line), "OTA BEGIN %lu %s",
           static_cast<unsigned long>(imageSize), safeVersion.c_str());
  sendBridgeBuffer(line, strlen(line));
  startPicoAckWait("ota_begin", "ota_begin", kPicoOtaBeginTimeoutMs);
}

static void sendPicoOtaChunkAsync(uint32_t offset, const uint8_t *data, size_t len) {
  char line[32 + (kPicoOtaChunkBytes * 2u)];
  const int prefix = snprintf(line, sizeof(line), "OTA CHUNK %lu ",
                              static_cast<unsigned long>(offset));
  if (prefix <= 0) return;
  size_t pos = static_cast<size_t>(prefix);
  for (size_t i = 0; i < len && (pos + 2u) < sizeof(line); ++i) {
    static const char kHex[] = "0123456789ABCDEF";
    line[pos++] = kHex[(data[i] >> 4) & 0x0F];
    line[pos++] = kHex[data[i] & 0x0F];
  }
  sendBridgeBuffer(line, pos);
  startPicoAckWait("ota_chunk", "ota_chunk", kPicoOtaChunkTimeoutMs);
}

static void finishPicoOtaAsync(uint32_t crc32) {
  char buf[16];
  snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(crc32));
  char line[32];
  const int written = snprintf(line, sizeof(line), "OTA END %s", buf);
  if (written > 0) sendBridgeBuffer(line, static_cast<size_t>(written));
  startPicoAckWait("ota_end", "ota_end", kPicoOtaFinishTimeoutMs);
}

static void servicePicoOtaTransfer() {
  if (!gPicoUpload.active || gPicoUpload.failed || gPicoUpload.step == PicoTransferStep::Staging) return;

  if (gPicoUpload.step == PicoTransferStep::Queued) {
    if (!gSpiffsReady) {
      failPicoUpload("ESP staging storage unavailable");
      return;
    }
    closePicoStageFile();
    gPicoStageFile = SPIFFS.open(kPicoOtaStagePath, FILE_READ);
    if (!gPicoStageFile) {
      failPicoUpload("Failed to reopen staged Pico image");
      return;
    }

    String message;
    if (!preflightPicoOta(&message)) {
      failPicoUpload(message);
      return;
    }

    quiesceBridgeForPicoOtaStart();
    beginPicoOtaAsync(gPicoUpload.totalBytes, gPicoUpload.versionTag);
    gPicoUpload.step = PicoTransferStep::BeginWait;
    gPicoUpload.lastMessage = "ESP staged image, starting transfer to RP2040";
    gPicoUpload.lastProgressMs = 0;
    setBridgeStatusLocal("PICO OTA TX");
    return;
  }

  if (gPicoUpload.step == PicoTransferStep::BeginWait) {
    String message;
    const AckPollResult ack = pollPicoAckWait(&message);
    if (ack == AckPollResult::Pending) return;
    if (ack == AckPollResult::Fail) {
      failPicoUpload(message, true);
      return;
    }
    gPicoUpload.lastMessage = message;
    gPicoUpload.step = PicoTransferStep::ChunkPrepare;
    return;
  }

  if (gPicoUpload.step == PicoTransferStep::ChunkPrepare) {
    if (gPicoUpload.sentBytes >= gPicoUpload.totalBytes) {
      finishPicoOtaAsync(gPicoUpload.crc32);
      gPicoUpload.step = PicoTransferStep::EndWait;
      gPicoUpload.lastMessage = "RP2040 image transfer complete, finalizing";
      setBridgeStatusLocal("PICO OTA VERIFY");
      return;
    }

    uint8_t buf[kPicoOtaChunkBytes];
    const size_t remaining = static_cast<size_t>(gPicoUpload.totalBytes - gPicoUpload.sentBytes);
    const size_t want = remaining < kPicoOtaChunkBytes ? remaining : kPicoOtaChunkBytes;
    const size_t got = gPicoStageFile.read(buf, want);
    if (got == 0u) {
      failPicoUpload("Staged Pico image ended unexpectedly", true);
      return;
    }

    gPicoUpload.pendingChunkOffset = gPicoUpload.sentBytes;
    gPicoUpload.pendingChunkLen = static_cast<uint32_t>(got);
    gPicoUpload.pendingChunkCrc32 = crc32Update(gPicoUpload.crc32, buf, got);
    sendPicoOtaChunkAsync(gPicoUpload.pendingChunkOffset, buf, got);
    gPicoUpload.step = PicoTransferStep::ChunkWait;
    return;
  }

  if (gPicoUpload.step == PicoTransferStep::ChunkWait) {
    String message;
    const AckPollResult ack = pollPicoAckWait(&message);
    if (ack == AckPollResult::Pending) return;
    if (ack == AckPollResult::Fail) {
      failPicoUpload(message, true);
      return;
    }

    gPicoUpload.crc32 = gPicoUpload.pendingChunkCrc32;
    gPicoUpload.sentBytes += gPicoUpload.pendingChunkLen;
    gPicoUpload.pendingChunkLen = 0u;
    gPicoUpload.lastMessage = message;

    const unsigned long now = millis();
    if ((now - gPicoUpload.lastProgressMs) >= kOtaStepMs && gPicoUpload.totalBytes > 0u) {
      const unsigned long pct =
          (static_cast<unsigned long long>(gPicoUpload.sentBytes) * 100ull) / gPicoUpload.totalBytes;
      setBridgeStatusLocal(String("PICO ") + pct + "%");
      gPicoUpload.lastProgressMs = now;
    }

    gPicoUpload.step = PicoTransferStep::ChunkPrepare;
    return;
  }

  if (gPicoUpload.step == PicoTransferStep::EndWait) {
    String message;
    const AckPollResult ack = pollPicoAckWait(&message);
    if (ack == AckPollResult::Pending) return;
    if (ack == AckPollResult::Fail) {
      failPicoUpload(message);
      return;
    }

    clearPicoStageFile();
    gPicoUpload.lastMessage = message;
    gPicoUpload.step = PicoTransferStep::Done;
    gPicoUpload.active = false;
    setBridgeStatusLocal("PICO OTA OK");
  }
}

static bool fetchLogSlice(uint32_t start, uint32_t count, uint32_t timeoutMs = kRequestTimeoutMs) {
  if (!bridgeRequestsAllowed()) return false;
  gCache.logEvents.clear();
  gCache.logMetaReady = false;
  gCache.logPending = true;
  gCache.logExpected = 0u;
  sendBridgeLine(String("GET LOG ") + start + " " + count);

  const unsigned long deadline = millis() + timeoutMs;
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    serviceBackground();
    if (gCache.logMetaReady && !gCache.logPending) return true;
  }

  return gCache.logMetaReady && !gCache.logPending;
}

static String buildSystemJson() {
  String out = "{";
  out += "\"ok\":1,";
  out += "\"rp_mode\":\"" + jsonEscape(gCache.rpMode) + "\",";
  out += "\"bridge_mode\":\"" + String(bridgeModeName(gBridgeMode)) + "\",";
  out += "\"wifi_mode\":\"" + String(wifiProfileModeName(gNetCfg.wifiMode)) + "\",";
  out += "\"hostname\":\"" + jsonEscape(gNetCfg.hostname) + "\",";
  out += "\"status\":\"" + jsonEscape(gBridgeStatus) + "\",";
  out += "\"link_up\":";
  out += gCache.linkUp ? "true" : "false";
  out += ",\"ap_ip\":\"" + jsonEscape(ipOrEmpty(WiFi.softAPIP())) + "\",";
  out += "\"sta_ip\":\"" + jsonEscape(ipOrEmpty(WiFi.localIP())) + "\",";
  out += "\"hello_counter\":";
  out += gCache.helloCounter;
  out += ",\"telemetry_counter\":";
  out += gCache.telemetryCounter;
  out += ",\"stats_counter\":";
  out += gCache.statsCounter;
  out += ",\"settings_counter\":";
  out += gCache.settingsCounter;
  out += ",\"ota_counter\":";
  out += gCache.otaCounter;
  out += ",\"ack_counter\":";
  out += gCache.ackCounter;
  out += ",\"error_counter\":";
  out += gCache.errorCounter;
  out += ",\"ntp_enabled\":";
  out += gNetCfg.ntpEnabled ? "true" : "false";
  out += ",\"ntp_synced\":";
  out += gNtpSynced ? "true" : "false";
  out += ",\"ntp_epoch\":";
  out += String((unsigned long)gNtpEpoch);
  out += ",\"ntp_age_ms\":";
  out += gNtpSynced ? String(millis() - gNtpSyncMs) : String(-1);
  out += ",\"pico_ota_mode\":";
  out += bridgeCacheWantsPicoOtaMode() ? "true" : "false";
  out += ",\"pico_ota_ready\":";
  out += (!gCache.otaJson.isEmpty() && jsonBoolValue(gCache.otaJson, "safe_ready")) ? "true" : "false";
  out += "}";
  return out;
}

static String buildNetworkConfigJson() {
  String out = "{";
  out += "\"ok\":1,";
  out += "\"wifi_mode\":\"" + String(wifiProfileModeName(gNetCfg.wifiMode)) + "\",";
  out += "\"hostname\":\"" + jsonEscape(gNetCfg.hostname) + "\",";
  out += "\"ap_ssid\":\"" + jsonEscape(gNetCfg.apSsid) + "\",";
  out += "\"sta_ssid\":\"" + jsonEscape(gNetCfg.staSsid) + "\",";
  out += "\"ap_password_set\":";
  out += gNetCfg.apPassword.isEmpty() ? "false" : "true";
  out += ",\"sta_password_set\":";
  out += gNetCfg.staPassword.isEmpty() ? "false" : "true";
  out += ",\"ota_password_set\":";
  out += gNetCfg.otaPassword.isEmpty() ? "false" : "true";
  out += ",\"ntp_enabled\":";
  out += gNetCfg.ntpEnabled ? "true" : "false";
  out += ",\"ntp_server\":\"" + jsonEscape(gNetCfg.ntpServer) + "\"";
  out += ",\"tz_offset\":" + String(gNetCfg.tzOffsetSec);
  out += ",\"admin_password_set\":";
  out += gNetCfg.adminPassword.isEmpty() ? "false" : "true";
  out += "}";
  return out;
}

static String buildCacheJson() {
  String out = "{";
  out += "\"hello\":";
  out += gCache.helloJson.isEmpty() ? "null" : gCache.helloJson;
  out += ",\"telemetry\":";
  out += gCache.telemetryJson.isEmpty() ? "null" : gCache.telemetryJson;
  out += ",\"stats\":";
  out += gCache.statsJson.isEmpty() ? "null" : gCache.statsJson;
  out += ",\"settings\":";
  out += gCache.settingsJson.isEmpty() ? "null" : gCache.settingsJson;
  out += ",\"ota\":";
  out += gCache.otaJson.isEmpty() ? "null" : gCache.otaJson;
  out += ",\"last_ack\":";
  out += gCache.lastAckJson.isEmpty() ? "null" : gCache.lastAckJson;
  out += ",\"last_error\":";
  out += gCache.lastErrorJson.isEmpty() ? "null" : gCache.lastErrorJson;
  out += "}";
  return out;
}

static String buildPicoOtaStatusJson() {
  requestOtaIfNeeded(false);
  String out = "{";
  out += "\"upload_active\":";
  out += gPicoUpload.active ? "true" : "false";
  out += ",\"upload_failed\":";
  out += gPicoUpload.failed ? "true" : "false";
  out += ",\"phase\":\"";
  out += picoUploadPhaseName();
  out += "\"";
  out += ",\"staged_bytes\":";
  out += gPicoUpload.stagedBytes;
  out += ",\"sent_bytes\":";
  out += gPicoUpload.sentBytes;
  out += ",\"total_bytes\":";
  out += gPicoUpload.totalBytes;
  out += ",\"upload_crc32\":\"";
  char crcBuf[16];
  snprintf(crcBuf, sizeof(crcBuf), "%08lX", static_cast<unsigned long>(gPicoUpload.crc32));
  out += crcBuf;
  out += "\",\"version\":\"";
  out += jsonEscape(gPicoUpload.versionTag);
  out += "\",\"message\":\"";
  out += jsonEscape(gPicoUpload.lastMessage);
  out += "\",\"hostname\":\"";
  out += jsonEscape(gNetCfg.hostname);
  out += "\",\"ap_ip\":\"";
  out += jsonEscape(ipOrEmpty(WiFi.softAPIP()));
  out += "\",\"device\":";
  out += gCache.otaJson.isEmpty() ? "null" : gCache.otaJson;
  out += "}";
  return out;
}

static String buildLogSliceResponse() {
  String out = "{\"total\":";
  out += gCache.logTotal;
  out += ",\"start\":";
  out += gCache.logStart;
  out += ",\"count\":";
  out += gCache.logEvents.size();
  out += ",\"events\":[";
  for (size_t i = 0; i < gCache.logEvents.size(); ++i) {
    if (i) out += ",";
    out += gCache.logEvents[i];
  }
  out += "]}";
  return out;
}

static String statsCsv() {
  requestTelemetryIfNeeded(false);
  requestStatsIfNeeded(false);
  requestSettingsIfNeeded(false);

  String out;
  out += "rp_mode,soc_pct,available_wh,time_min,voltage_v,current_a,power_w,temp_bat_c,temp_inv_c,charging,";
  out += "boot_count,efc_total,energy_in_wh,energy_out_wh,runtime_h,temp_avg_c,temp_min_c,temp_max_c,peak_current_a,peak_power_w,soh_last,";
  out += "capacity_ah,vbat_warn_v,vbat_cut_v,cell_warn_v,cell_cut_v,temp_bat_warn_c,temp_bat_cut_c\n";
  out += csvEscape(gCache.rpMode) + ",";
  out += csvEscape(jsonValue(gCache.telemetryJson, "soc_pct")) + ",";
  out += csvEscape(jsonValue(gCache.telemetryJson, "available_wh")) + ",";
  out += csvEscape(jsonValue(gCache.telemetryJson, "time_min")) + ",";
  out += csvEscape(jsonValue(gCache.telemetryJson, "voltage_v")) + ",";
  out += csvEscape(jsonValue(gCache.telemetryJson, "current_a")) + ",";
  out += csvEscape(jsonValue(gCache.telemetryJson, "power_w")) + ",";
  out += csvEscape(jsonValue(gCache.telemetryJson, "temp_bat_c")) + ",";
  out += csvEscape(jsonValue(gCache.telemetryJson, "temp_inv_c")) + ",";
  out += csvEscape(jsonValue(gCache.telemetryJson, "charging")) + ",";
  out += csvEscape(jsonValue(gCache.statsJson, "boot_count")) + ",";
  out += csvEscape(jsonValue(gCache.statsJson, "efc_total")) + ",";
  out += csvEscape(jsonValue(gCache.statsJson, "energy_in_wh")) + ",";
  out += csvEscape(jsonValue(gCache.statsJson, "energy_out_wh")) + ",";
  out += csvEscape(jsonValue(gCache.statsJson, "runtime_h")) + ",";
  out += csvEscape(jsonValue(gCache.statsJson, "temp_avg_c")) + ",";
  out += csvEscape(jsonValue(gCache.statsJson, "temp_min_c")) + ",";
  out += csvEscape(jsonValue(gCache.statsJson, "temp_max_c")) + ",";
  out += csvEscape(jsonValue(gCache.statsJson, "peak_current_a")) + ",";
  out += csvEscape(jsonValue(gCache.statsJson, "peak_power_w")) + ",";
  out += csvEscape(jsonValue(gCache.statsJson, "soh_last")) + ",";
  out += csvEscape(jsonValue(gCache.settingsJson, "capacity_ah")) + ",";
  out += csvEscape(jsonValue(gCache.settingsJson, "vbat_warn_v")) + ",";
  out += csvEscape(jsonValue(gCache.settingsJson, "vbat_cut_v")) + ",";
  out += csvEscape(jsonValue(gCache.settingsJson, "cell_warn_v")) + ",";
  out += csvEscape(jsonValue(gCache.settingsJson, "cell_cut_v")) + ",";
  out += csvEscape(jsonValue(gCache.settingsJson, "temp_bat_warn_c")) + ",";
  out += csvEscape(jsonValue(gCache.settingsJson, "temp_bat_cut_c")) + "\n";
  return out;
}

static String logCsvRow(const String &eventJson) {
  String row;
  row += csvEscape(jsonValue(eventJson, "seq")) + ",";
  row += csvEscape(jsonValue(eventJson, "idx")) + ",";
  row += csvEscape(jsonValue(eventJson, "time")) + ",";
  row += csvEscape(jsonValue(eventJson, "ts")) + ",";
  row += csvEscape(jsonValue(eventJson, "is_epoch")) + ",";
  row += csvEscape(jsonValue(eventJson, "kind_name")) + ",";
  row += csvEscape(jsonValue(eventJson, "soc_pct")) + ",";
  row += csvEscape(jsonValue(eventJson, "temp_bat")) + ",";
  row += csvEscape(jsonValue(eventJson, "temp_inv")) + ",";
  row += csvEscape(jsonValue(eventJson, "voltage_v")) + ",";
  row += csvEscape(jsonValue(eventJson, "current_a")) + ",";
  row += csvEscape(jsonValue(eventJson, "param")) + ",";
  row += csvEscape(jsonValue(eventJson, "param_label")) + ",";
  row += csvEscape(jsonValue(eventJson, "alarm_names")) + "\n";
  return row;
}

static void sendJsonError(int code, const String &message) {
  gServer.send(code, "application/json",
               String("{\"ok\":0,\"message\":\"") + jsonEscape(message) + "\"}");
}

static void sendJsonResult(bool ok, const String &message) {
  gServer.send(200, "application/json",
               String("{\"ok\":") + (ok ? "1" : "0") + ",\"message\":\"" +
                   jsonEscape(message) + "\"}");
}

static bool requireAuth() {
  if (gNetCfg.adminPassword.isEmpty()) return true;
  if (!gServer.authenticate("admin", gNetCfg.adminPassword.c_str())) {
    gServer.requestAuthentication(BASIC_AUTH, "PowerStation");
    return false;
  }
  return true;
}

static void handleIndex() {
  if (!requireAuth()) return;
  if (picoOtaUiModeActive()) {
    gServer.sendHeader("Location", "/pico-ota");
    gServer.send(302, "text/plain", "");
    return;
  }
  gServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  gServer.sendHeader("Pragma", "no-cache");
  gServer.sendHeader("Expires", "0");
  gServer.send_P(200, "text/html", PSTATION_WEB_UI);
}

static void handlePicoOtaPage() {
  if (!picoOtaUiModeActive()) {
    gServer.sendHeader("Location", "/");
    gServer.send(302, "text/plain", "");
    return;
  }
  gServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  gServer.sendHeader("Pragma", "no-cache");
  gServer.sendHeader("Expires", "0");
  gServer.send_P(200, "text/html", PSTATION_PICO_OTA_UI);
}

static void handleSystem() {
  if (rejectIfGeneralUiLocked()) return;
  gServer.send(200, "application/json", buildSystemJson());
}

static void handleTelemetry() {
  if (rejectIfGeneralUiLocked()) return;
  if (!requestTelemetryIfNeeded(false) || gCache.telemetryJson.isEmpty()) {
    sendJsonError(503, "Telemetry unavailable");
    return;
  }
  gServer.send(200, "application/json", gCache.telemetryJson);
}

static void handleStats() {
  if (rejectIfGeneralUiLocked()) return;
  if (!requestStatsIfNeeded(false) || gCache.statsJson.isEmpty()) {
    sendJsonError(503, "Stats unavailable");
    return;
  }
  gServer.send(200, "application/json", gCache.statsJson);
}

static void handleRpSettings() {
  if (rejectIfGeneralUiLocked()) return;
  if (!requestSettingsIfNeeded(false) || gCache.settingsJson.isEmpty()) {
    sendJsonError(503, "RP2040 settings unavailable");
    return;
  }
  gServer.send(200, "application/json", gCache.settingsJson);
}

static void handleNetworkSettings() {
  if (rejectIfGeneralUiLocked()) return;
  gServer.send(200, "application/json", buildNetworkConfigJson());
}

static void handleNetworkSettingsPost() {
  if (rejectIfGeneralUiLocked()) return;
  gNetCfg.wifiMode = wifiProfileModeFromText(gServer.arg("wifi_mode"));
  if (gServer.hasArg("hostname")) gNetCfg.hostname = gServer.arg("hostname");
  if (gServer.hasArg("ap_ssid")) gNetCfg.apSsid = gServer.arg("ap_ssid");
  if (gServer.hasArg("sta_ssid")) gNetCfg.staSsid = gServer.arg("sta_ssid");
  if (gServer.hasArg("ap_password") && !gServer.arg("ap_password").isEmpty()) gNetCfg.apPassword = gServer.arg("ap_password");
  if (gServer.hasArg("sta_password") && !gServer.arg("sta_password").isEmpty()) gNetCfg.staPassword = gServer.arg("sta_password");
  if (gServer.hasArg("ota_password") && !gServer.arg("ota_password").isEmpty()) gNetCfg.otaPassword = gServer.arg("ota_password");
  if (gServer.hasArg("ntp_enabled")) gNetCfg.ntpEnabled = (gServer.arg("ntp_enabled") == "true" || gServer.arg("ntp_enabled") == "1");
  if (gServer.hasArg("ntp_server") && !gServer.arg("ntp_server").isEmpty()) gNetCfg.ntpServer = gServer.arg("ntp_server");
  if (gServer.hasArg("tz_offset")) gNetCfg.tzOffsetSec = gServer.arg("tz_offset").toInt();
  if (gServer.hasArg("admin_password")) gNetCfg.adminPassword = gServer.arg("admin_password");

  if (gNetCfg.apSsid.isEmpty()) gNetCfg.apSsid = defaultApSsid();
  if (gNetCfg.apPassword.length() < 8) gNetCfg.apPassword = "powerstat";
  if (gNetCfg.hostname.isEmpty()) gNetCfg.hostname = defaultHostname();

  saveNetworkConfig();
  applyNetworkConfig();
  sendJsonResult(true, "Network profile saved and applied");
}

static void handleRpApplySetting() {
  if (rejectIfGeneralUiLocked()) return;
  const String key = gServer.arg("key");
  const String value = gServer.arg("value");
  String message;

  if (key.isEmpty() || value.isEmpty()) {
    sendJsonError(400, "Missing key or value");
    return;
  }

  const bool ok = applyRpSetting(key, value, &message);
  sendJsonResult(ok, message);
}

static void handlePorts() {
  if (rejectIfGeneralUiLocked()) return;
  // Request fresh port state from RP2040
  const uint32_t before = gCache.portsCounter;
  sendBridgeLine("GET PORTS");
  if (!waitForCounter(&gCache.portsCounter, before, kRequestTimeoutMs) || gCache.portsJson.isEmpty()) {
    sendJsonError(503, "Ports unavailable");
    return;
  }
  gServer.send(200, "application/json", gCache.portsJson);
}

static void handlePortsPost() {
  if (rejectIfGeneralUiLocked()) return;
  const String port = gServer.arg("port");
  const String state = gServer.arg("state");
  String message;

  if (port.isEmpty() || state.isEmpty()) {
    sendJsonError(400, "Missing port or state");
    return;
  }
  if (port != "dc_out" && port != "usb_pd") {
    sendJsonError(400, "Invalid port (allowed: dc_out, usb_pd)");
    return;
  }

  sendBridgeLine(String("ACTION PORT_SET ") + port + " " + state);
  const bool ok = waitForAckOrError("port_set", "action", &message);
  sendJsonResult(ok, message);
}

static void handleActions() {
  if (rejectIfGeneralUiLocked()) return;
  const String action = gServer.arg("name");
  String message;

  if (action.isEmpty()) {
    sendJsonError(400, "Missing action name");
    return;
  }

  const bool ok = applyRpAction(action, &message);
  sendJsonResult(ok, message);
}

static void handleLogSlice() {
  if (rejectIfGeneralUiLocked()) return;
  const uint32_t start = static_cast<uint32_t>(gServer.hasArg("start") ? gServer.arg("start").toInt() : 0);
  const uint32_t count = static_cast<uint32_t>(gServer.hasArg("count") ? gServer.arg("count").toInt() : kLogBatchSize);
  if (!fetchLogSlice(start, count)) {
    sendJsonError(504, "Timed out waiting for log slice");
    return;
  }
  gServer.send(200, "application/json", buildLogSliceResponse());
}

static void handleCache() {
  if (rejectIfGeneralUiLocked()) return;
  requestTelemetryIfNeeded(false);
  requestStatsIfNeeded(false);
  requestSettingsIfNeeded(false);
  gServer.send(200, "application/json", buildCacheJson());
}

static void handleStatsJsonExport() {
  if (rejectIfGeneralUiLocked()) return;
  requestTelemetryIfNeeded(false);
  requestStatsIfNeeded(false);
  requestSettingsIfNeeded(false);

  String out = "{";
  out += "\"system\":";
  out += buildSystemJson();
  out += ",\"telemetry\":";
  out += gCache.telemetryJson.isEmpty() ? "null" : gCache.telemetryJson;
  out += ",\"stats\":";
  out += gCache.statsJson.isEmpty() ? "null" : gCache.statsJson;
  out += ",\"settings\":";
  out += gCache.settingsJson.isEmpty() ? "null" : gCache.settingsJson;
  out += "}";

  gServer.sendHeader("Content-Disposition", "attachment; filename=powerstation-stats.json");
  gServer.send(200, "application/json", out);
}

static void handleStatsCsvExport() {
  if (rejectIfGeneralUiLocked()) return;
  gServer.sendHeader("Content-Disposition", "attachment; filename=powerstation-stats.csv");
  gServer.send(200, "text/csv", statsCsv());
}

static void streamLogJsonExport() {
  if (rejectIfGeneralUiLocked()) return;
  if (!fetchLogSlice(0, kLogBatchSize)) {
    sendJsonError(504, "Timed out waiting for first log slice");
    return;
  }

  const uint32_t total = gCache.logTotal;
  gServer.sendHeader("Content-Disposition", "attachment; filename=powerstation-log.json");
  gServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  gServer.send(200, "application/json", "");
  gServer.sendContent(String("{\"total\":") + total + ",\"events\":[");

  bool first = true;
  uint32_t start = 0;
  while (start < total) {
    if (start != 0 && !fetchLogSlice(start, kLogBatchSize, 2600)) break;
    for (const String &eventJson : gCache.logEvents) {
      if (!first) gServer.sendContent(",");
      gServer.sendContent(eventJson);
      first = false;
    }
    if (gCache.logEvents.empty()) break;
    start += static_cast<uint32_t>(gCache.logEvents.size());
    serviceBackground();
  }

  gServer.sendContent("]}");
  gServer.sendContent("");
}

static void streamLogCsvExport() {
  if (rejectIfGeneralUiLocked()) return;
  if (!fetchLogSlice(0, kLogBatchSize)) {
    sendJsonError(504, "Timed out waiting for first log slice");
    return;
  }

  const uint32_t total = gCache.logTotal;
  gServer.sendHeader("Content-Disposition", "attachment; filename=powerstation-log.csv");
  gServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  gServer.send(200, "text/csv", "");
  gServer.sendContent("seq,idx,time,ts_sec,is_epoch,event,soc_pct,temp_bat_c,temp_inv_c,voltage_v,current_a,param,param_type,alarms\n");

  uint32_t start = 0;
  while (start < total) {
    if (start != 0 && !fetchLogSlice(start, kLogBatchSize, 2600)) break;
    for (const String &eventJson : gCache.logEvents) {
      gServer.sendContent(logCsvRow(eventJson));
    }
    if (gCache.logEvents.empty()) break;
    start += static_cast<uint32_t>(gCache.logEvents.size());
    serviceBackground();
  }

  gServer.sendContent("");
}

static void handleOtaUploadFinished() {
  const bool ok = !Update.hasError();
  sendJsonResult(ok, ok ? "OTA upload complete, rebooting" : "OTA upload failed");
  if (ok) {
    sendBridgeStatus("OTA OK");
    delay(250);
    ESP.restart();
  } else {
    sendBridgeStatus("OTA FAIL");
  }
}

static void handlePicoOtaStatus() {
  gServer.send(200, "application/json", buildPicoOtaStatusJson());
}

static void resetPicoUploadSession(const String &message, bool keepFailure) {
  clearPicoStageFile();
  resetPicoUploadRequest();
  gPicoUpload.active = false;
  gPicoUpload.step = keepFailure ? PicoTransferStep::Failed : PicoTransferStep::Idle;
  gPicoUpload.failed = keepFailure;
  gPicoUpload.waitExpectedCmd = nullptr;
  gPicoUpload.waitContext = nullptr;
  gPicoUpload.waitDeadlineMs = 0;
  gPicoUpload.lastMessage = message;
}

static void handlePicoOtaStart() {
  if (gBridgeMode != BridgeMode::PicoOta && !picoUploadSessionPinned()) {
    sendJsonError(409, "RP2040 bridge mode is not Pico OTA");
    return;
  }

  uint32_t imageSize = 0u;
  const String sizeArg = gServer.arg("size");
  const String versionTag = sanitizeVersionTag(gServer.arg("version"));
  if (!parseUint32Arg(sizeArg, &imageSize)) {
    sendJsonError(400, "Missing or invalid Pico image size");
    return;
  }

  String message;
  if (!startStagedPicoUpload(imageSize, versionTag, &message)) {
    sendJsonError(409, message);
    return;
  }
  sendJsonResult(true, message);
}

static void handlePicoOtaChunkFinished() {
  const bool ok = !gPicoUpload.failed &&
                  gPicoUpload.active &&
                  gPicoUpload.step == PicoTransferStep::Staging &&
                  !gPicoUpload.requestOpen;
  sendJsonResult(ok, ok ? "Pico chunk staged on ESP." : gPicoUpload.lastMessage);
}

static void handlePicoOtaChunkUpload() {
  HTTPUpload &upload = gServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (gBridgeMode != BridgeMode::PicoOta && !picoUploadSessionPinned()) return;
  } else if (!gPicoUpload.requestOpen) {
    return;
  }

  if (upload.status == UPLOAD_FILE_START) {
    uint32_t offset = 0u;
    uint32_t expectedBytes = 0u;
    if (!gPicoUpload.active || gPicoUpload.failed || gPicoUpload.step != PicoTransferStep::Staging) {
      gPicoUpload.failed = true;
      gPicoUpload.lastMessage = "Pico OTA staging session is not active";
      gPicoUpload.step = PicoTransferStep::Failed;
      setBridgeStatusLocal("PICO OTA FAIL");
      return;
    }
    if (!parseUint32Arg(gServer.arg("offset"), &offset)) {
      gPicoUpload.failed = true;
      gPicoUpload.lastMessage = "Missing or invalid Pico chunk offset";
      gPicoUpload.step = PicoTransferStep::Failed;
      setBridgeStatusLocal("PICO OTA FAIL");
      return;
    }
    if (!parseUint32Arg(gServer.arg("size"), &expectedBytes)) {
      gPicoUpload.failed = true;
      gPicoUpload.lastMessage = "Missing or invalid Pico chunk size";
      gPicoUpload.step = PicoTransferStep::Failed;
      setBridgeStatusLocal("PICO OTA FAIL");
      return;
    }
    if (offset != gPicoUpload.stagedBytes) {
      gPicoUpload.failed = true;
      gPicoUpload.lastMessage = "Unexpected Pico chunk offset";
      gPicoUpload.step = PicoTransferStep::Failed;
      setBridgeStatusLocal("PICO OTA FAIL");
      return;
    }
    gPicoUpload.requestOpen = true;
    gPicoUpload.requestOffset = offset;
    gPicoUpload.requestExpectedBytes = expectedBytes;
    gPicoUpload.requestReceivedBytes = 0u;
    gPicoUpload.lastMessage = "Receiving Pico chunk on ESP";
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!gPicoUpload.active || gPicoUpload.failed || gPicoUpload.step != PicoTransferStep::Staging) return;
    String message;
    const uint32_t chunkOffset = gPicoUpload.requestOffset + gPicoUpload.requestReceivedBytes;
    if (!appendStagedPicoChunk(upload.buf, upload.currentSize, chunkOffset, &message)) {
      failPicoUpload(message);
      return;
    }
    gPicoUpload.requestReceivedBytes += static_cast<uint32_t>(upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!gPicoUpload.requestOpen) return;
    if (!gPicoUpload.failed &&
        gPicoUpload.requestExpectedBytes != 0u &&
        gPicoUpload.requestReceivedBytes != gPicoUpload.requestExpectedBytes) {
      failPicoUpload("Pico chunk size mismatch");
      return;
    }
    resetPicoUploadRequest();
    if (!gPicoUpload.failed) {
      gPicoUpload.lastMessage = "Pico chunk staged on ESP";
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    failPicoUpload("Browser chunk upload aborted");
    setBridgeStatusLocal("PICO OTA ABORT");
  }
}

static void handlePicoOtaCommit() {
  if (gBridgeMode != BridgeMode::PicoOta && !picoUploadSessionPinned()) {
    sendJsonError(409, "RP2040 bridge mode is not Pico OTA");
    return;
  }
  String message;
  if (!commitStagedPicoUpload(&message)) {
    sendJsonError(409, message);
    return;
  }
  sendJsonResult(true, message);
}

static void handlePicoOtaAbort() {
  if (gPicoUpload.active) abortPicoOta("browser_abort");
  resetPicoUploadSession("Pico OTA session cancelled", false);
  setBridgeStatusLocal("PICO OTA ABORT");
  sendJsonResult(true, "Pico OTA session cancelled");
}

static void handleOtaUploadChunk() {
  if (gBridgeMode != BridgeMode::Ota) return;

  HTTPUpload &upload = gServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    gLastOtaStatusMs = 0;
    sendBridgeStatus("OTA WEB");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
    const unsigned long now = millis();
    if ((now - gLastOtaStatusMs) >= kOtaStepMs) {
      sendBridgeStatus("OTA WRITE");
      gLastOtaStatusMs = now;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    sendBridgeStatus("OTA ABORT");
  }
}

static void handleOtaUploadGuarded() {
  if (gBridgeMode != BridgeMode::Ota) {
    sendJsonError(409, "RP2040 bridge mode is not OTA");
    return;
  }
  handleOtaUploadFinished();
}

static void handleTime() {
  struct tm ti;
  time_t now = time(nullptr);
  localtime_r(&now, &ti);
  char buf[128];
  snprintf(buf, sizeof(buf),
    "{\"ok\":1,\"epoch\":%lu,\"year\":%d,\"month\":%d,\"day\":%d,\"hour\":%d,\"min\":%d,\"sec\":%d,\"synced\":%s}",
    (unsigned long)now, ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
    ti.tm_hour, ti.tm_min, ti.tm_sec,
    now > 1700000000 ? "true" : "false");
  gServer.send(200, "application/json", buf);
}

static void handleCli() {
  if (rejectIfGeneralUiLocked()) return;
  const String cmd = gServer.arg("cmd");
  if (cmd.isEmpty()) {
    sendJsonError(400, "Missing cmd parameter");
    return;
  }
  gCache.cliLines.clear();
  gCache.cliCapturing = true;
  sendBridgeLine(cmd);

  const unsigned long deadline = millis() + 800u;
  while (millis() < deadline) {
    serviceBackground();
  }
  gCache.cliCapturing = false;

  String out = "{\"ok\":1,\"lines\":[";
  for (size_t i = 0; i < gCache.cliLines.size(); ++i) {
    if (i) out += ",";
    out += "\"";
    out += jsonEscape(gCache.cliLines[i]);
    out += "\"";
  }
  out += "]}";
  gServer.send(200, "application/json", out);
}

/* Auth-wrapping helper: wraps a handler so requireAuth() is checked first */
#define AUTH(fn)  []() { if (!requireAuth()) return; fn(); }

static void setupRoutes() {
  gServer.on("/", HTTP_GET, handleIndex);
  gServer.on("/pico-ota", HTTP_GET, AUTH(handlePicoOtaPage));
  gServer.on("/api/system", HTTP_GET, AUTH(handleSystem));
  gServer.on("/api/telemetry", HTTP_GET, AUTH(handleTelemetry));
  gServer.on("/api/stats", HTTP_GET, AUTH(handleStats));
  gServer.on("/api/settings/rp2040", HTTP_GET, AUTH(handleRpSettings));
  gServer.on("/api/settings/network", HTTP_GET, AUTH(handleNetworkSettings));
  gServer.on("/api/settings/network", HTTP_POST, AUTH(handleNetworkSettingsPost));
  gServer.on("/api/settings/rp2040/apply", HTTP_POST, AUTH(handleRpApplySetting));
  gServer.on("/api/actions", HTTP_POST, AUTH(handleActions));
  gServer.on("/api/cli", HTTP_POST, AUTH(handleCli));
  gServer.on("/api/time", HTTP_GET, AUTH(handleTime));
  gServer.on("/api/ports", HTTP_GET, AUTH(handlePorts));
  gServer.on("/api/ports", HTTP_POST, AUTH(handlePortsPost));
  gServer.on("/api/log", HTTP_GET, AUTH(handleLogSlice));
  gServer.on("/api/cache", HTTP_GET, AUTH(handleCache));
  gServer.on("/api/pico-ota/status", HTTP_GET, AUTH(handlePicoOtaStatus));
  gServer.on("/api/pico-ota/start", HTTP_POST, AUTH(handlePicoOtaStart));
  gServer.on("/api/pico-ota/chunk", HTTP_POST, AUTH(handlePicoOtaChunkFinished), handlePicoOtaChunkUpload);
  gServer.on("/api/pico-ota/commit", HTTP_POST, AUTH(handlePicoOtaCommit));
  gServer.on("/api/pico-ota/abort", HTTP_POST, AUTH(handlePicoOtaAbort));
  gServer.on("/export/stats.json", HTTP_GET, AUTH(handleStatsJsonExport));
  gServer.on("/export/stats.csv", HTTP_GET, AUTH(handleStatsCsvExport));
  gServer.on("/export/log.json", HTTP_GET, AUTH(streamLogJsonExport));
  gServer.on("/export/log.csv", HTTP_GET, AUTH(streamLogCsvExport));
  gServer.on("/api/ota/upload", HTTP_POST, AUTH(handleOtaUploadGuarded), handleOtaUploadChunk);
  gServer.on("/api/pico-ota/upload", HTTP_POST, []() {
    sendJsonError(410, "Legacy Pico upload route is disabled. Reload the Pico OTA page.");
  });

  gServer.onNotFound([]() {
    if (!requireAuth()) return;
    if (gServer.uri().startsWith("/api/") || gServer.uri().startsWith("/export/")) {
      sendJsonError(404, "Not found");
      return;
    }
    if (picoOtaUiModeActive()) {
      gServer.sendHeader("Location", "/pico-ota");
      gServer.send(302, "text/plain", "");
      return;
    }
    handleIndex();
  });
}

static void periodicBridgeTasks() {
  const unsigned long now = millis();
  const bool picoUploadBusy = gPicoUpload.active;
  const bool picoOtaMode = bridgeCacheWantsPicoOtaMode();

  if (gCache.linkUp && (now - gCache.lastRxMs) > kLinkTimeoutMs) {
    gCache.linkUp = false;
    if (gBridgeMode == BridgeMode::PicoOta) sendBridgeStatus("PICO OTA WAIT");
    else sendBridgeStatus(gBridgeMode == BridgeMode::Ota ? "OTA WAIT" : "WEB WAIT");
  }

  if (!picoUploadBusy && (now - gLastHeartbeatMs) >= kHeartbeatMs) {
    sendBridgeLine("PING");
    gLastHeartbeatMs = now;
  }
  if (!picoUploadBusy && (now - gLastHelloReqMs) >= kHelloMs) {
    sendBridgeLine("HELLO");
    gLastHelloReqMs = now;
  }
  if (!picoUploadBusy && !picoOtaMode && (now - gLastStatsReqMs) >= kStatsRefreshMs) {
    sendBridgeLine("GET STATS");
    gLastStatsReqMs = now;
  }
  if (!picoUploadBusy && !picoOtaMode && (now - gLastSettingsReqMs) >= kSettingsRefreshMs) {
    sendBridgeLine("GET SETTINGS");
    gLastSettingsReqMs = now;
  }
  if (!picoUploadBusy && (now - gLastOtaReqMs) >= kOtaRefreshMs) {
    sendBridgeLine("GET OTA");
    gLastOtaReqMs = now;
  }
  if (!picoUploadBusy &&
      gBridgeMode != BridgeMode::PicoOta &&
      (now - gLastTelemetryReqMs) >= kTelemetryRefreshMs &&
      (now - gCache.lastTelemetryMs) >= kTelemetryRefreshMs) {
    sendBridgeLine("GET TELEMETRY");
    gLastTelemetryReqMs = now;
  }
  /* Sync real-time to Pico when NTP is available */
  if (!picoUploadBusy && gNetCfg.ntpEnabled && (now - gLastTimeSyncMs) >= kTimeSyncMs) {
    time_t epoch = time(nullptr);
    const bool timeValid = epoch > 1700000000;
    if (timeValid) {
      gNtpSynced = true;
      gNtpEpoch = epoch;
      gNtpSyncMs = now;
      char buf[40];
      snprintf(buf, sizeof(buf), "SET time %lu", (unsigned long)epoch);
      sendBridgeLine(buf);
    } else if ((now - gNtpSyncMs) > 300000) {  // 5 minutes without valid time
      gNtpSynced = false;
    }
    gLastTimeSyncMs = now;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("PowerStation ESP32-S2 Bridge boot");

  gPrefs.begin("pstation", false);
  loadNetworkConfig();
  gSpiffsReady = SPIFFS.begin(true);
  clearPicoStageFile();

  gBridge.setRxBufferSize(2048);
  gBridge.begin(PSTATION_UART_BAUD, SERIAL_8N1, PSTATION_UART_RX_PIN, PSTATION_UART_TX_PIN);

  applyNetworkConfig();
  setupRoutes();
  gServer.begin();

  // mDNS: powerstation.local
  {
    String host = gNetCfg.hostname;
    if (host.isEmpty()) host = "powerstation";
    if (MDNS.begin(host.c_str())) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("mDNS: " + host + ".local");
    }
  }

  // Captive DNS: resolve powerstation.web (and any other) to AP IP
  gDns.setTTL(300);
  gDns.start(53, "*", WiFi.softAPIP());

  sendBridgeStatus("ESP BOOT");
  sendBridgeLine("HELLO");
  sendBridgeLine("GET SETTINGS");
  sendBridgeLine("GET STATS");
  sendBridgeLine("GET OTA");
}

void loop() {
  gDns.processNextRequest();
  pollBridgeRx();
  servicePicoOtaTransfer();
  periodicBridgeTasks();
  if (gBridgeMode == BridgeMode::Ota) ArduinoOTA.handle();
  gServer.handleClient();
  delay(2);
}
