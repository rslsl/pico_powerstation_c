#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include <vector>

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

constexpr size_t kUartLineMax = 640;
constexpr uint32_t kHeartbeatMs = 5000;
constexpr uint32_t kHelloMs = 2000;
constexpr uint32_t kStatsRefreshMs = 15000;
constexpr uint32_t kSettingsRefreshMs = 30000;
constexpr uint32_t kTelemetryRefreshMs = 4000;
constexpr uint32_t kLinkTimeoutMs = 15000;
constexpr uint32_t kRequestTimeoutMs = 1800;
constexpr uint32_t kOtaStepMs = 600;
constexpr uint32_t kLogBatchSize = 16;

enum class BridgeMode : uint8_t {
  Off = 0,
  Web = 1,
  Ota = 2,
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
};

struct BridgeCache {
  String helloJson;
  String telemetryJson;
  String statsJson;
  String settingsJson;
  String lastAckJson;
  String lastErrorJson;
  String rpMode = "OFF";

  bool linkUp = false;
  uint32_t helloCounter = 0;
  uint32_t telemetryCounter = 0;
  uint32_t statsCounter = 0;
  uint32_t settingsCounter = 0;
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
};

Preferences gPrefs;
WebServer gServer(80);
HardwareSerial gBridge(1);
NetworkConfig gNetCfg;
BridgeCache gCache;

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
unsigned long gLastOtaStatusMs = 0;

}  // namespace

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
  gBridge.print(line);
  gBridge.print('\n');
}

static void sendBridgeStatus(const String &status) {
  const String clipped = status.substring(0, 23);
  gBridgeStatus = clipped;
  if (clipped == gLastStatusSent) return;
  sendBridgeLine(String("STATUS ") + clipped);
  gLastStatusSent = clipped;
}

static void loadNetworkConfig() {
  gNetCfg.wifiMode = static_cast<WifiProfileMode>(gPrefs.getUChar("wifi_mode", static_cast<uint8_t>(WifiProfileMode::Ap)));
  gNetCfg.staSsid = gPrefs.getString("sta_ssid", "");
  gNetCfg.staPassword = gPrefs.getString("sta_pass", "");
  gNetCfg.apSsid = gPrefs.getString("ap_ssid", defaultApSsid());
  gNetCfg.apPassword = gPrefs.getString("ap_pass", "powerstat");
  gNetCfg.hostname = gPrefs.getString("host", defaultHostname());
  gNetCfg.otaPassword = gPrefs.getString("ota_pass", "");

  if (gNetCfg.apSsid.isEmpty()) gNetCfg.apSsid = defaultApSsid();
  if (gNetCfg.apPassword.length() < 8) gNetCfg.apPassword = "powerstat";
  if (gNetCfg.hostname.isEmpty()) gNetCfg.hostname = defaultHostname();
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
}

static void announceModeStatus() {
  if (gBridgeMode == BridgeMode::Ota) sendBridgeStatus("OTA READY");
  else if (gBridgeMode == BridgeMode::Web) sendBridgeStatus("WEB READY");
  else sendBridgeStatus("BRIDGE IDLE");
}

static void applyBridgeModeFromRp(const String &modeText) {
  if (modeText.isEmpty()) return;
  gCache.rpMode = modeText;
  const BridgeMode next = bridgeModeFromText(modeText);
  if (next != gBridgeMode) {
    gBridgeMode = next;
    announceModeStatus();
  } else if (gBridgeStatus == "ESP BOOT" || gBridgeStatus == "WEB WAIT" || gBridgeStatus == "OTA WAIT") {
    announceModeStatus();
  }
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

  WiFi.setHostname(gNetCfg.hostname.c_str());

  if (wantAp) {
    WiFi.softAP(gNetCfg.apSsid.c_str(), gNetCfg.apPassword.c_str());
  }
  if (wantSta) {
    WiFi.begin(gNetCfg.staSsid.c_str(), gNetCfg.staPassword.c_str());
  }

  configureArduinoOta();
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
  const uint32_t before = gCache.statsCounter;
  sendBridgeLine("GET STATS");
  gLastStatsReqMs = now;
  return waitForCounter(&gCache.statsCounter, before, kRequestTimeoutMs);
}

static bool requestSettingsIfNeeded(bool force) {
  const unsigned long now = millis();
  if (!force && !gCache.settingsJson.isEmpty() && (now - gCache.lastSettingsMs) < kSettingsRefreshMs) return true;
  const uint32_t before = gCache.settingsCounter;
  sendBridgeLine("GET SETTINGS");
  gLastSettingsReqMs = now;
  return waitForCounter(&gCache.settingsCounter, before, kRequestTimeoutMs);
}

static bool requestTelemetryIfNeeded(bool force) {
  const unsigned long now = millis();
  if (!force && !gCache.telemetryJson.isEmpty() && (now - gCache.lastTelemetryMs) < kTelemetryRefreshMs) return true;
  const uint32_t before = gCache.telemetryCounter;
  sendBridgeLine("GET TELEMETRY");
  gLastTelemetryReqMs = now;
  return waitForCounter(&gCache.telemetryCounter, before, kRequestTimeoutMs);
}

static bool waitForAckOrError(String *messageOut) {
  const uint32_t ackBefore = gCache.ackCounter;
  const uint32_t errBefore = gCache.errorCounter;
  const unsigned long deadline = millis() + kRequestTimeoutMs;

  while (static_cast<int32_t>(deadline - millis()) > 0) {
    serviceBackground();
    if (gCache.errorCounter != errBefore) {
      if (messageOut) *messageOut = gCache.lastErrorJson;
      return false;
    }
    if (gCache.ackCounter != ackBefore) {
      if (messageOut) *messageOut = gCache.lastAckJson;
      return jsonBoolValue(gCache.lastAckJson, "ok");
    }
  }

  if (messageOut) *messageOut = "{\"type\":\"timeout\"}";
  return false;
}

static bool applyRpSetting(const String &key, const String &value, String *messageOut) {
  sendBridgeLine(String("SET ") + key + " " + value);
  return waitForAckOrError(messageOut);
}

static bool applyRpAction(const String &action, String *messageOut) {
  sendBridgeLine(String("ACTION ") + action);
  return waitForAckOrError(messageOut);
}

static bool fetchLogSlice(uint32_t start, uint32_t count, uint32_t timeoutMs = kRequestTimeoutMs) {
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
  out += ",\"ack_counter\":";
  out += gCache.ackCounter;
  out += ",\"error_counter\":";
  out += gCache.errorCounter;
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
  out += ",\"last_ack\":";
  out += gCache.lastAckJson.isEmpty() ? "null" : gCache.lastAckJson;
  out += ",\"last_error\":";
  out += gCache.lastErrorJson.isEmpty() ? "null" : gCache.lastErrorJson;
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
  row += csvEscape(jsonValue(eventJson, "idx")) + ",";
  row += csvEscape(jsonValue(eventJson, "ts")) + ",";
  row += csvEscape(jsonValue(eventJson, "kind")) + ",";
  row += csvEscape(jsonValue(eventJson, "soc_pct")) + ",";
  row += csvEscape(jsonValue(eventJson, "temp_bat")) + ",";
  row += csvEscape(jsonValue(eventJson, "voltage_v")) + ",";
  row += csvEscape(jsonValue(eventJson, "current_a")) + ",";
  row += csvEscape(jsonValue(eventJson, "param")) + ",";
  row += csvEscape(jsonValue(eventJson, "alarms")) + "\n";
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

static void handleIndex() {
  gServer.send_P(200, "text/html", PSTATION_WEB_UI);
}

static void handleSystem() {
  gServer.send(200, "application/json", buildSystemJson());
}

static void handleTelemetry() {
  if (!requestTelemetryIfNeeded(false) || gCache.telemetryJson.isEmpty()) {
    sendJsonError(503, "Telemetry unavailable");
    return;
  }
  gServer.send(200, "application/json", gCache.telemetryJson);
}

static void handleStats() {
  if (!requestStatsIfNeeded(false) || gCache.statsJson.isEmpty()) {
    sendJsonError(503, "Stats unavailable");
    return;
  }
  gServer.send(200, "application/json", gCache.statsJson);
}

static void handleRpSettings() {
  if (!requestSettingsIfNeeded(false) || gCache.settingsJson.isEmpty()) {
    sendJsonError(503, "RP2040 settings unavailable");
    return;
  }
  gServer.send(200, "application/json", gCache.settingsJson);
}

static void handleNetworkSettings() {
  gServer.send(200, "application/json", buildNetworkConfigJson());
}

static void handleNetworkSettingsPost() {
  gNetCfg.wifiMode = wifiProfileModeFromText(gServer.arg("wifi_mode"));
  if (gServer.hasArg("hostname")) gNetCfg.hostname = gServer.arg("hostname");
  if (gServer.hasArg("ap_ssid")) gNetCfg.apSsid = gServer.arg("ap_ssid");
  if (gServer.hasArg("sta_ssid")) gNetCfg.staSsid = gServer.arg("sta_ssid");
  if (gServer.hasArg("ap_password") && !gServer.arg("ap_password").isEmpty()) gNetCfg.apPassword = gServer.arg("ap_password");
  if (gServer.hasArg("sta_password") && !gServer.arg("sta_password").isEmpty()) gNetCfg.staPassword = gServer.arg("sta_password");
  if (gServer.hasArg("ota_password") && !gServer.arg("ota_password").isEmpty()) gNetCfg.otaPassword = gServer.arg("ota_password");

  if (gNetCfg.apSsid.isEmpty()) gNetCfg.apSsid = defaultApSsid();
  if (gNetCfg.apPassword.length() < 8) gNetCfg.apPassword = "powerstat";
  if (gNetCfg.hostname.isEmpty()) gNetCfg.hostname = defaultHostname();

  saveNetworkConfig();
  applyNetworkConfig();
  sendJsonResult(true, "Network profile saved and applied");
}

static void handleRpApplySetting() {
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

static void handleActions() {
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
  const uint32_t start = static_cast<uint32_t>(gServer.hasArg("start") ? gServer.arg("start").toInt() : 0);
  const uint32_t count = static_cast<uint32_t>(gServer.hasArg("count") ? gServer.arg("count").toInt() : kLogBatchSize);
  if (!fetchLogSlice(start, count)) {
    sendJsonError(504, "Timed out waiting for log slice");
    return;
  }
  gServer.send(200, "application/json", buildLogSliceResponse());
}

static void handleCache() {
  requestTelemetryIfNeeded(false);
  requestStatsIfNeeded(false);
  requestSettingsIfNeeded(false);
  gServer.send(200, "application/json", buildCacheJson());
}

static void handleStatsJsonExport() {
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
  gServer.sendHeader("Content-Disposition", "attachment; filename=powerstation-stats.csv");
  gServer.send(200, "text/csv", statsCsv());
}

static void streamLogJsonExport() {
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
  if (!fetchLogSlice(0, kLogBatchSize)) {
    sendJsonError(504, "Timed out waiting for first log slice");
    return;
  }

  const uint32_t total = gCache.logTotal;
  gServer.sendHeader("Content-Disposition", "attachment; filename=powerstation-log.csv");
  gServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  gServer.send(200, "text/csv", "");
  gServer.sendContent("idx,ts,kind,soc_pct,temp_bat,voltage_v,current_a,param,alarms\n");

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

static void setupRoutes() {
  gServer.on("/", HTTP_GET, handleIndex);
  gServer.on("/api/system", HTTP_GET, handleSystem);
  gServer.on("/api/telemetry", HTTP_GET, handleTelemetry);
  gServer.on("/api/stats", HTTP_GET, handleStats);
  gServer.on("/api/settings/rp2040", HTTP_GET, handleRpSettings);
  gServer.on("/api/settings/network", HTTP_GET, handleNetworkSettings);
  gServer.on("/api/settings/network", HTTP_POST, handleNetworkSettingsPost);
  gServer.on("/api/settings/rp2040/apply", HTTP_POST, handleRpApplySetting);
  gServer.on("/api/actions", HTTP_POST, handleActions);
  gServer.on("/api/log", HTTP_GET, handleLogSlice);
  gServer.on("/api/cache", HTTP_GET, handleCache);
  gServer.on("/export/stats.json", HTTP_GET, handleStatsJsonExport);
  gServer.on("/export/stats.csv", HTTP_GET, handleStatsCsvExport);
  gServer.on("/export/log.json", HTTP_GET, streamLogJsonExport);
  gServer.on("/export/log.csv", HTTP_GET, streamLogCsvExport);
  gServer.on("/api/ota/upload", HTTP_POST, handleOtaUploadGuarded, handleOtaUploadChunk);

  gServer.onNotFound([]() {
    if (gServer.uri().startsWith("/api/") || gServer.uri().startsWith("/export/")) {
      sendJsonError(404, "Not found");
      return;
    }
    handleIndex();
  });
}

static void periodicBridgeTasks() {
  const unsigned long now = millis();

  if (gCache.linkUp && (now - gCache.lastRxMs) > kLinkTimeoutMs) {
    gCache.linkUp = false;
    sendBridgeStatus(gBridgeMode == BridgeMode::Ota ? "OTA WAIT" : "WEB WAIT");
  }

  if ((now - gLastHeartbeatMs) >= kHeartbeatMs) {
    sendBridgeLine("PING");
    gLastHeartbeatMs = now;
  }
  if ((now - gLastHelloReqMs) >= kHelloMs) {
    sendBridgeLine("HELLO");
    gLastHelloReqMs = now;
  }
  if ((now - gLastStatsReqMs) >= kStatsRefreshMs) {
    sendBridgeLine("GET STATS");
    gLastStatsReqMs = now;
  }
  if ((now - gLastSettingsReqMs) >= kSettingsRefreshMs) {
    sendBridgeLine("GET SETTINGS");
    gLastSettingsReqMs = now;
  }
  if ((now - gLastTelemetryReqMs) >= kTelemetryRefreshMs && (now - gCache.lastTelemetryMs) >= kTelemetryRefreshMs) {
    sendBridgeLine("GET TELEMETRY");
    gLastTelemetryReqMs = now;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("PowerStation ESP32-S2 Bridge boot");

  gPrefs.begin("pstation", false);
  loadNetworkConfig();

  gBridge.setRxBufferSize(2048);
  gBridge.begin(PSTATION_UART_BAUD, SERIAL_8N1, PSTATION_UART_RX_PIN, PSTATION_UART_TX_PIN);

  applyNetworkConfig();
  setupRoutes();
  gServer.begin();

  sendBridgeStatus("ESP BOOT");
  sendBridgeLine("HELLO");
  sendBridgeLine("GET SETTINGS");
  sendBridgeLine("GET STATS");
}

void loop() {
  pollBridgeRx();
  periodicBridgeTasks();
  if (gBridgeMode == BridgeMode::Ota) ArduinoOTA.handle();
  gServer.handleClient();
  delay(2);
}
