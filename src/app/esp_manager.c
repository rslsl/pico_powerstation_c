#include "esp_manager.h"
#include "buzzer.h"

#include "../config.h"

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "pico/stdlib.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void _status_set(EspManager *esp, const char *text) {
    if (!esp) return;
    strncpy(esp->last_status, text ? text : "", sizeof(esp->last_status) - 1u);
    esp->last_status[sizeof(esp->last_status) - 1u] = '\0';
}

static bool _streq_icase(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool _starts_with_icase(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    while (*prefix) {
        if (*s == '\0') return false;
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
        ++s;
        ++prefix;
    }
    return true;
}

static bool _status_is_transient(const char *status) {
    return !status || status[0] == '\0' ||
           _streq_icase(status, "ONLINE") ||
           _streq_icase(status, "BOOT HOLD") ||
           _streq_icase(status, "BOOT WAIT") ||
           _streq_icase(status, "WAIT LINK") ||
           _streq_icase(status, "WEB BOOT") ||
           _streq_icase(status, "OTA BOOT");
}

static bool _boot_keep_esp_requested(void) {
    return watchdog_hw->scratch[BOOTCTL_WATCHDOG_KEEP_ESP_SCRATCH] == BOOTCTL_WATCHDOG_KEEP_ESP_MAGIC;
}

static void _boot_keep_esp_clear(void) {
    watchdog_hw->scratch[BOOTCTL_WATCHDOG_KEEP_ESP_SCRATCH] = 0u;
}

static float _clampf_local(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float _display_soc_pct(const BatSnapshot *snap) {
    const SystemSettings *cfg = settings_get();
    float fallback_pct;
    if (!snap) return 0.0f;

    fallback_pct = _clampf_local(snap->soc, 0.0f, 100.0f);
    if (!cfg ||
        isnan(snap->voltage) || isinf(snap->voltage) ||
        isnan(cfg->pack_full_v) || isinf(cfg->pack_full_v) ||
        isnan(cfg->vbat_cut_v) || isinf(cfg->vbat_cut_v) ||
        cfg->pack_full_v <= (cfg->vbat_cut_v + 0.05f)) {
        return fallback_pct;
    }

    return _clampf_local((snap->voltage - cfg->vbat_cut_v) /
                         (cfg->pack_full_v - cfg->vbat_cut_v) * 100.0f,
                         0.0f, 100.0f);
}

static void _power_set(EspManager *esp, bool on, uint32_t now_ms) {
    if (!esp || !esp->uart_ready) return;
    if (on == esp->powered) return;

    gpio_put(GPIO_ESP_EN, on ? ESP_EN_ON : ESP_EN_OFF);
    esp->powered = on;
    esp->link_up = false;
    esp->last_rx_ms = 0u;
    esp->last_hello_ms = 0u;
    esp->last_telemetry_ms = 0u;
    esp->rx_len = 0u;
    esp->ready_at_ms = on ? (now_ms + ESP_BOOT_SETTLE_MS) : 0u;
}

static void _uart_write_line(EspManager *esp, const char *line) {
    if (!esp || !esp->uart_ready || !line) return;
    uart_puts(uart1, line);
    uart_puts(uart1, "\n");
    esp->tx_frames++;
}

static void _send_ack(EspManager *esp, const char *cmd, const char *detail, bool ok) {
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"ack\",\"cmd\":\"%s\",\"detail\":\"%s\",\"ok\":%u}",
             cmd ? cmd : "",
             detail ? detail : "",
             ok ? 1u : 0u);
    _uart_write_line(esp, buf);
}

static void _send_error(EspManager *esp, const char *reason) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"error\",\"reason\":\"%s\"}",
             reason ? reason : "unknown");
    _uart_write_line(esp, buf);
}

static void _send_help(EspManager *esp) {
    _uart_write_line(
        esp,
        "{\"type\":\"help\",\"commands\":\"PING|HELLO|STATUS <text>|GET TELEMETRY|GET STATS|GET SETTINGS|GET PORTS|GET LOG <start> <count>|GET OTA|SET <key> <value>|ACTION LOG_RESET|ACTION STATS_RESET|ACTION PORT_SET <port> <on|off|toggle>|ACTION SHUTDOWN|OTA BEGIN <size> <version>|OTA CHUNK <offset> <hex>|OTA END <crc32>|OTA ABORT\"}");
}

static void _send_hello(EspManager *esp) {
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"hello\",\"fw\":\"%s\",\"mode\":\"%s\",\"uart_baud\":%u}",
             FW_VERSION,
             esp_mode_name(esp_mode_get(esp)),
             (unsigned)ESP_UART_BAUD_HZ);
    _uart_write_line(esp, buf);
}

static void _send_telemetry(EspManager *esp, const BatSnapshot *snap, uint32_t now_ms) {
    char buf[256];
    float display_soc_pct;
    if (!esp || !snap) return;
    display_soc_pct = _display_soc_pct(snap);
    snprintf(buf, sizeof(buf),
             "{\"type\":\"telemetry\",\"uptime_ms\":%lu,\"mode\":\"%s\",\"soc_pct\":%.1f,\"available_wh\":%.1f,\"time_min\":%d,"
             "\"voltage_v\":%.3f,\"current_a\":%.3f,\"power_w\":%.1f,\"temp_bat_c\":%.1f,\"temp_inv_c\":%.1f,\"charging\":%u}",
             (unsigned long)now_ms,
             esp_mode_name(esp_mode_get(esp)),
             display_soc_pct,
             snap->remaining_wh,
             snap->time_min,
             snap->voltage,
             snap->i_net,
             snap->power_w,
             snap->temp_bat,
             snap->temp_inv,
             snap->is_charging ? 1u : 0u);
    _uart_write_line(esp, buf);
}

static void _send_stats(EspManager *esp) {
    char buf[512];
    const StatsFlash *f;
    if (!esp || !esp->stats || !esp->stats->initialized) {
        _send_error(esp, "stats_unavailable");
        return;
    }
    f = &esp->stats->flash;
    snprintf(buf, sizeof(buf),
             "{\"type\":\"stats\",\"boot_count\":%lu,\"efc_total\":%.3f,\"energy_in_wh\":%.1f,\"energy_out_wh\":%.1f,"
             "\"runtime_h\":%.2f,\"temp_avg_c\":%.1f,\"temp_min_c\":%.1f,\"temp_max_c\":%.1f,\"peak_current_a\":%.1f,"
             "\"peak_power_w\":%.1f,\"soh_last\":%.1f,\"alarm_events\":%lu,\"ocp_events\":%lu,\"temp_events\":%lu}",
             (unsigned long)f->boot_count,
             f->efc_total,
             f->energy_in_wh,
             f->energy_out_wh,
             f->runtime_h,
             stats_avg_temp_c(esp->stats),
             f->temp_min_c,
             f->temp_max_c,
             f->peak_current_a,
             f->peak_power_w,
             f->soh_last,
             (unsigned long)f->total_alarm_events,
             (unsigned long)f->total_ocp_events,
             (unsigned long)f->total_temp_events);
    _uart_write_line(esp, buf);
}

static const char *_power_policy_name(PowerPolicy policy) {
    switch (policy) {
        case PWR_POLICY_LOADS_ON:    return "loads_on";
        case PWR_POLICY_CHARGE_ONLY: return "charge_only";
        case PWR_POLICY_FAULT_LATCH: return "fault_latch";
        case PWR_POLICY_ISOLATED:
        default:                     return "isolated";
    }
}

static const char *_port_lock_reason(const PowerControl *pwr, PortId port) {
    if (!pwr) return "unavailable";
    if (pwr_is_safe_mode(pwr) && (port == PORT_DC_OUT || port == PORT_USB_PD)) {
        return "safe_mode";
    }
    switch (pwr_policy_get(pwr)) {
        case PWR_POLICY_CHARGE_ONLY: return "charge_only";
        case PWR_POLICY_FAULT_LATCH: return "fault_latch";
        case PWR_POLICY_ISOLATED:    return "isolated";
        case PWR_POLICY_LOADS_ON:
        default:                     return "";
    }
}

static void _send_settings(EspManager *esp) {
    char buf[1024];
    const SystemSettings *cfg = settings_get();
    const EspMode runtime_mode = esp_mode_get(esp);
    const PicoMode runtime_pico_mode = esp->pico_ota_ready ? PICO_MODE_OTA_SAFE : PICO_MODE_NORMAL;
    snprintf(buf, sizeof(buf),
             "{\"type\":\"settings\",\"capacity_ah\":%.2f,\"pack_full_v\":%.2f,\"pack_empty_v\":%.2f,\"pack_nominal_v\":%.2f,"
             "\"vbat_warn_v\":%.2f,\"vbat_cut_v\":%.2f,\"cell_warn_v\":%.3f,\"cell_cut_v\":%.3f,"
             "\"temp_bat_warn_c\":%.1f,\"temp_bat_buzz_c\":%.1f,\"temp_bat_safe_c\":%.1f,\"temp_bat_cut_c\":%.1f,"
             "\"temp_bat_charge_min_c\":%.1f,\"temp_inv_warn_c\":%.1f,\"temp_inv_safe_c\":%.1f,\"temp_inv_cut_c\":%.1f,"
             "\"shunt_dis_mohm\":%.3f,\"shunt_chg_mohm\":%.3f,"
             "\"pack_dis_v_gain\":%.4f,\"pack_chg_v_gain\":%.4f,\"cell1_v_gain\":%.4f,\"cell2_v_gain\":%.4f,\"cell3_v_gain\":%.4f,"
             "\"cal_ref_dis_current_a\":%.3f,\"cal_ref_dis_voltage_v\":%.3f,\"cal_ref_chg_current_a\":%.3f,\"cal_ref_chg_voltage_v\":%.3f,"
             "\"cal_ref_cell1_v\":%.3f,\"cal_ref_cell2_v\":%.3f,\"cal_ref_cell3_v\":%.3f,"
             "\"buzzer_en\":%u,\"buzzer_preset\":%u,\"ui_brightness\":%u,"
             "\"esp_mode\":\"%s\",\"esp_mode_saved\":\"%s\",\"pico_mode\":\"%s\"}",
             cfg->capacity_ah,
             cfg->pack_full_v,
             cfg->pack_empty_v,
             cfg->pack_nominal_v,
             cfg->vbat_warn_v,
             cfg->vbat_cut_v,
             cfg->cell_warn_v,
             cfg->cell_cut_v,
             cfg->temp_bat_warn_c,
             cfg->temp_bat_buzz_c,
             cfg->temp_bat_safe_c,
             cfg->temp_bat_cut_c,
             cfg->temp_bat_charge_min_c,
             cfg->temp_inv_warn_c,
             cfg->temp_inv_safe_c,
             cfg->temp_inv_cut_c,
             cfg->shunt_dis_mohm,
             cfg->shunt_chg_mohm,
             cfg->pack_dis_v_gain,
             cfg->pack_chg_v_gain,
             cfg->cell1_v_gain,
             cfg->cell2_v_gain,
             cfg->cell3_v_gain,
             cfg->cal_ref_dis_current_a,
             cfg->cal_ref_dis_voltage_v,
             cfg->cal_ref_chg_current_a,
             cfg->cal_ref_chg_voltage_v,
             cfg->cal_ref_cell1_v,
             cfg->cal_ref_cell2_v,
             cfg->cal_ref_cell3_v,
             (unsigned)cfg->buzzer_en,
             (unsigned)cfg->buzzer_preset,
             (unsigned)cfg->ui_brightness,
             esp_mode_name(runtime_mode),
             esp_mode_name((EspMode)cfg->esp_mode),
             pico_mode_name(runtime_pico_mode));
    _uart_write_line(esp, buf);
}

static void _send_ports(EspManager *esp) {
    char buf[512];
    const PowerControl *pwr = esp ? esp->pwr : NULL;
    const bool dc_desired = pwr ? pwr_user_desired_on(pwr, PORT_DC_OUT) : false;
    const bool dc_on = pwr ? pwr_is_on(pwr, PORT_DC_OUT) : false;
    const bool usb_desired = pwr ? pwr_user_desired_on(pwr, PORT_USB_PD) : false;
    const bool usb_on = pwr ? pwr_is_on(pwr, PORT_USB_PD) : false;
    const bool fan_desired = pwr ? pwr_user_desired_on(pwr, PORT_FAN) : false;
    const bool fan_on = pwr ? pwr_is_on(pwr, PORT_FAN) : false;
    snprintf(buf, sizeof(buf),
             "{\"type\":\"ports\",\"safe_mode\":%u,\"policy\":\"%s\","
             "\"dc_out_on\":%u,\"dc_out_desired\":%u,\"dc_out_lock\":\"%s\","
             "\"usb_pd_on\":%u,\"usb_pd_desired\":%u,\"usb_pd_lock\":\"%s\","
             "\"fan_on\":%u,\"fan_desired\":%u,\"fan_mode\":\"AUTO\"}",
             pwr ? (pwr_is_safe_mode(pwr) ? 1u : 0u) : 0u,
             pwr ? _power_policy_name(pwr_policy_get(pwr)) : "unavailable",
             dc_on ? 1u : 0u,
             dc_desired ? 1u : 0u,
             _port_lock_reason(pwr, PORT_DC_OUT),
             usb_on ? 1u : 0u,
             usb_desired ? 1u : 0u,
             _port_lock_reason(pwr, PORT_USB_PD),
             fan_on ? 1u : 0u,
             fan_desired ? 1u : 0u);
    _uart_write_line(esp, buf);
}

static void _send_ota_status(EspManager *esp) {
    char buf[448];
    PicoOtaStatus status;
    const PicoMode runtime_pico_mode = esp->pico_ota_ready ? PICO_MODE_OTA_SAFE : PICO_MODE_NORMAL;
    pico_ota_fill_status(&esp->ota, &status);
    snprintf(buf, sizeof(buf),
             "{\"type\":\"ota\",\"state\":\"%s\",\"running_slot\":\"%s\",\"target_slot\":\"%s\",\"active_slot\":\"%s\","
             "\"pending_slot\":\"%s\",\"confirmed_slot\":\"%s\",\"size\":%lu,\"written\":%lu,\"crc32\":\"%08lX\","
             "\"reboot_pending\":%u,\"safe_ready\":%u,\"version\":\"%s\",\"pico_mode\":\"%s\",\"error\":\"%s\"}",
             pico_ota_state_name(status.state),
             pico_ota_slot_name(status.running_slot),
             pico_ota_slot_name(status.target_slot),
             pico_ota_slot_name(status.active_slot),
             pico_ota_slot_name(status.pending_slot),
             pico_ota_slot_name(status.confirmed_slot),
             (unsigned long)status.image_size,
             (unsigned long)status.bytes_written,
             (unsigned long)status.expected_crc32,
             status.reboot_pending ? 1u : 0u,
             esp->pico_ota_ready ? 1u : 0u,
             status.version,
             pico_mode_name(runtime_pico_mode),
             status.last_error);
    _uart_write_line(esp, buf);
}

static const char *_log_kind_name(uint8_t type) {
    switch (type) {
        case LOG_BOOT:             return "BOOT";
        case LOG_SAVE_OK:          return "SAVE_OK";
        case LOG_SAVE_SKIP:        return "SAVE_SKIP";
        case LOG_BROWNOUT_ENTER:   return "BROWNOUT_ENTER";
        case LOG_BROWNOUT_EXIT:    return "BROWNOUT_EXIT";
        case LOG_SAFE_MODE_ENTER:  return "SAFE_MODE_ENTER";
        case LOG_SAFE_MODE_EXIT:   return "SAFE_MODE_EXIT";
        case LOG_CHARGER_PRESENT:  return "CHARGER_PRESENT";
        case LOG_CHARGER_LOST:     return "CHARGER_LOST";
        case LOG_CHARGE_START:     return "CHARGE_START";
        case LOG_CHARGE_END:       return "CHARGE_END";
        case LOG_DISCHARGE_START:  return "DISCHARGE_START";
        case LOG_DISCHARGE_END:    return "DISCHARGE_END";
        case LOG_PORT_CHANGED:     return "PORT_CHANGED";
        case LOG_ALARM_ENTER:      return "ALARM_ENTER";
        case LOG_ALARM_EXIT:       return "ALARM_EXIT";
        case LOG_ALARM_LEVEL:      return "ALARM_LEVEL";
        case LOG_OCP_EVENT:        return "OCP";
        case LOG_SOC_WARN:         return "SOC_WARN";
        case LOG_TEMP_WARN:        return "TEMP_WARN";
        case LOG_FAN_ON:           return "FAN_ON";
        case LOG_FAN_OFF:          return "FAN_OFF";
        case LOG_FAN_BLOCKED:      return "FAN_BLOCKED";
        case LOG_SENSOR_FAULT:     return "SENSOR_FAULT";
        case LOG_SENSOR_RECOVERED: return "SENSOR_RECOVERED";
        case LOG_I2C_RECOVERY:     return "I2C_RECOVERY";
        default:                   return "UNKNOWN";
    }
}

static const char *_log_param_label(uint8_t type) {
    switch (type) {
        case LOG_CHARGE_END:
        case LOG_DISCHARGE_END: return "energy_wh";
        case LOG_ALARM_ENTER:
        case LOG_ALARM_EXIT:
        case LOG_ALARM_LEVEL:   return "alarm_flags";
        case LOG_FAN_ON:
        case LOG_FAN_OFF:       return "temp_inv";
        case LOG_I2C_RECOVERY:  return "fail_count";
        default:                return "value";
    }
}

static int _format_uptime(uint32_t ts, char *out, int maxlen) {
    uint32_t d = ts / 86400u;
    uint32_t h = (ts % 86400u) / 3600u;
    uint32_t m = (ts % 3600u) / 60u;
    uint32_t s = ts % 60u;
    if (d > 0) return snprintf(out, maxlen, "%lud %luh %lum", (unsigned long)d, (unsigned long)h, (unsigned long)m);
    if (h > 0) return snprintf(out, maxlen, "%luh %lum %lus", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    return snprintf(out, maxlen, "%lum %lus", (unsigned long)m, (unsigned long)s);
}

static int _format_alarm_names(uint32_t flags, char *out, int maxlen) {
    static const struct { uint32_t bit; const char *name; } alarm_map[] = {
        { 1u <<  0, "SOC_WARN" },    { 1u <<  1, "SOC_CUT" },
        { 1u <<  2, "VBAT_WARN" },   { 1u <<  3, "VBAT_CUT" },
        { 1u <<  4, "CELL_WARN" },   { 1u <<  5, "CELL_CUT" },
        { 1u <<  6, "DELTA_WARN" },  { 1u <<  7, "DELTA_CUT" },
        { 1u <<  8, "OCP_WARN" },    { 1u <<  9, "OCP_CUT" },
        { 1u << 10, "TEMP_WARN" },   { 1u << 11, "TEMP_CUT" },
        { 1u << 12, "INV_WARN" },    { 1u << 13, "INV_CUT" },
        { 1u << 14, "I2C_FAULT" },   { 1u << 15, "CELL_OVP" },
        { 1u << 16, "COLD_CHARGE" }, { 1u << 17, "TEMP_BUZZ" },
        { 1u << 18, "TEMP_SAFE" },   { 1u << 19, "INV_SAFE" },
    };
    int pos = 0;
    if (flags == 0u) return snprintf(out, maxlen, "none");
    for (unsigned i = 0; i < sizeof(alarm_map) / sizeof(alarm_map[0]); ++i) {
        if (!(flags & alarm_map[i].bit)) continue;
        if (pos > 0 && pos < maxlen - 1) out[pos++] = ',';
        int n = snprintf(out + pos, maxlen - pos, "%s", alarm_map[i].name);
        if (n > 0) pos += n;
    }
    return pos;
}

static void _send_log_slice(EspManager *esp, uint32_t start, uint32_t count) {
    typedef struct {
        char line[512];
    } LogLine;
    LogLine lines[16];
    uint32_t sent = 0u;
    uint32_t total;

    if (!esp || !esp->logger) {
        _send_error(esp, "log_unavailable");
        return;
    }

    total = log_count(esp->logger);
    if (count == 0u) count = 1u;
    if (count > 16u) count = 16u;
    if (start >= total) {
        char meta[96];
        snprintf(meta, sizeof(meta),
                 "{\"type\":\"log_meta\",\"total\":%lu,\"start\":%lu,\"sent\":0}",
                 (unsigned long)total,
                 (unsigned long)start);
        _uart_write_line(esp, meta);
        return;
    }
    if (start + count > total) count = total - start;

    for (uint32_t i = 0; i < count && sent < 16u; ++i) {
        LogEvent ev;
        uint32_t idx = start + i;
        if (!log_read(esp->logger, idx, &ev)) {
            continue; /* skip corrupted/unreadable entries */
        }
        char time_str[32];
        char alarms[128];
        bool is_epoch = (ev.timestamp_s > 1700000000u);
        if (is_epoch) {
            /* Epoch → DD.MM.YYYY HH:MM */
            uint32_t ep = ev.timestamp_s;
            uint32_t days = ep / 86400u;
            uint32_t y = 1970; uint32_t rem = days;
            while (1) { uint32_t dy = (y%4==0&&(y%100!=0||y%400==0))?366:365; if(rem<dy)break; rem-=dy; y++; }
            uint32_t mon = 1; static const uint8_t mdays[]={31,28,31,30,31,30,31,31,30,31,30,31};
            bool leap = (y%4==0&&(y%100!=0||y%400==0));
            while (mon<=12) { uint32_t md=mdays[mon-1]; if(mon==2&&leap)md=29; if(rem<md)break; rem-=md; mon++; }
            uint32_t hh=(ep%86400u)/3600u, mm=(ep%3600u)/60u;
            snprintf(time_str, sizeof(time_str), "%02lu.%02lu.%lu %02lu:%02lu",
                     (unsigned long)(rem+1), (unsigned long)mon, (unsigned long)y,
                     (unsigned long)hh, (unsigned long)mm);
        } else {
            _format_uptime(ev.timestamp_s, time_str, sizeof(time_str));
        }
        _format_alarm_names(ev.flags, alarms, sizeof(alarms));
        snprintf(lines[sent].line, sizeof(lines[sent].line),
                 "{\"type\":\"log_event\",\"idx\":%lu,"
                 "\"ts\":%lu,\"is_epoch\":%u,\"time\":\"%s\","
                 "\"kind\":%u,\"kind_name\":\"%s\","
                 "\"cat\":%u,\"soc_pct\":%u,\"temp_bat\":%u,"
                 "\"voltage_v\":%.3f,\"current_a\":%.3f,"
                 "\"value1\":%.3f,\"param_label\":\"%s\","
                 "\"code\":%u,\"aux\":%u,"
                 "\"flags\":%lu,\"alarm_names\":\"%s\"}",
                 (unsigned long)idx,
                 (unsigned long)ev.timestamp_s,
                 is_epoch ? 1u : 0u,
                 time_str,
                 (unsigned)ev.type,
                 _log_kind_name(ev.type),
                 (unsigned)ev.category,
                 (unsigned)ev.soc_pct,
                 (unsigned)ev.temp_bat,
                 ev.voltage,
                 ev.current,
                 ev.value1,
                 _log_param_label(ev.type),
                 (unsigned)ev.code,
                 (unsigned)ev.aux,
                 (unsigned long)ev.flags,
                 alarms);
        sent++;
    }

    char meta[96];
    snprintf(meta, sizeof(meta),
             "{\"type\":\"log_meta\",\"total\":%lu,\"start\":%lu,\"sent\":%lu}",
             (unsigned long)total,
             (unsigned long)start,
             (unsigned long)sent);
    _uart_write_line(esp, meta);

    for (uint32_t i = 0; i < sent; ++i) {
        _uart_write_line(esp, lines[i].line);
    }
}

static bool _parse_float(const char *text, float *out) {
    char *end = NULL;
    float value;
    if (!text || !out) return false;
    value = strtof(text, &end);
    if (end == text || (end && *end != '\0')) return false;
    *out = value;
    return true;
}

static bool _parse_uint32(const char *text, uint32_t *out) {
    char *end = NULL;
    unsigned long value;
    if (!text || !out) return false;
    value = strtoul(text, &end, 10);
    if (end == text || (end && *end != '\0')) return false;
    *out = (uint32_t)value;
    return true;
}

static bool _parse_u32_auto(const char *text, uint32_t *out) {
    char *end = NULL;
    unsigned long value;
    if (!text || !out) return false;
    value = strtoul(text, &end, 0);
    if (end == text || (end && *end != '\0')) return false;
    *out = (uint32_t)value;
    return true;
}

static bool _parse_esp_mode_value(const char *text, uint8_t *out) {
    uint32_t mode = 0u;
    if (!text || !out) return false;
    if (_streq_icase(text, "off")) {
        *out = ESP_MODE_OFF;
        return true;
    }
    if (_streq_icase(text, "web")) {
        *out = ESP_MODE_WEB;
        return true;
    }
    if (_streq_icase(text, "ota")) {
        *out = ESP_MODE_OTA;
        return true;
    }
    if (!_parse_u32_auto(text, &mode) || mode >= ESP_MODE_COUNT) return false;
    *out = (uint8_t)mode;
    return true;
}

static int _hex_nibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

static bool _parse_hex_blob(const char *text, uint8_t *out, size_t max_len, size_t *out_len) {
    size_t len;
    if (!text || !out || !out_len) return false;
    len = strlen(text);
    if ((len & 1u) != 0u) return false;
    len /= 2u;
    if (len == 0u || len > max_len) return false;
    for (size_t i = 0; i < len; ++i) {
        int hi = _hex_nibble(text[i * 2u]);
        int lo = _hex_nibble(text[i * 2u + 1u]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    *out_len = len;
    return true;
}

static bool _apply_setting(EspManager *esp, const char *key, const char *value_text) {
    SystemSettings cfg;
    float value_f = 0.0f;
    bool reconfigure_sensors = false;
    uint8_t preset = BUZ_PRESET_FULL;
    uint8_t esp_mode = ESP_MODE_OFF;

    if (!esp || !esp->store_settings || !key || !value_text) return false;
    settings_copy(&cfg);

    if (_streq_icase(key, "capacity_ah")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.capacity_ah = value_f;
    } else if (_streq_icase(key, "pack_full_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.pack_full_v = value_f;
    } else if (_streq_icase(key, "pack_empty_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.pack_empty_v = value_f;
    } else if (_streq_icase(key, "pack_nominal_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.pack_nominal_v = value_f;
    } else if (_streq_icase(key, "vbat_warn_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.vbat_warn_v = value_f;
    } else if (_streq_icase(key, "vbat_cut_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.vbat_cut_v = value_f;
    } else if (_streq_icase(key, "cell_warn_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cell_warn_v = value_f;
    } else if (_streq_icase(key, "cell_cut_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cell_cut_v = value_f;
    } else if (_streq_icase(key, "temp_bat_warn_c")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.temp_bat_warn_c = value_f;
        reconfigure_sensors = true;
    } else if (_streq_icase(key, "temp_bat_buzz_c")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.temp_bat_buzz_c = value_f;
    } else if (_streq_icase(key, "temp_bat_safe_c")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.temp_bat_safe_c = value_f;
    } else if (_streq_icase(key, "temp_bat_cut_c")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.temp_bat_cut_c = value_f;
        reconfigure_sensors = true;
    } else if (_streq_icase(key, "temp_bat_charge_min_c")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.temp_bat_charge_min_c = value_f;
    } else if (_streq_icase(key, "temp_inv_warn_c")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.temp_inv_warn_c = value_f;
        reconfigure_sensors = true;
    } else if (_streq_icase(key, "temp_inv_safe_c")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.temp_inv_safe_c = value_f;
    } else if (_streq_icase(key, "temp_inv_cut_c")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.temp_inv_cut_c = value_f;
        reconfigure_sensors = true;
    } else if (_streq_icase(key, "shunt_dis_mohm")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.shunt_dis_mohm = value_f;
        reconfigure_sensors = true;
    } else if (_streq_icase(key, "shunt_chg_mohm")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.shunt_chg_mohm = value_f;
        reconfigure_sensors = true;
    } else if (_streq_icase(key, "pack_dis_v_gain")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.pack_dis_v_gain = value_f;
    } else if (_streq_icase(key, "pack_chg_v_gain")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.pack_chg_v_gain = value_f;
    } else if (_streq_icase(key, "cell1_v_gain")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cell1_v_gain = value_f;
    } else if (_streq_icase(key, "cell2_v_gain")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cell2_v_gain = value_f;
    } else if (_streq_icase(key, "cell3_v_gain")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cell3_v_gain = value_f;
    } else if (_streq_icase(key, "cal_ref_dis_current_a")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cal_ref_dis_current_a = value_f;
    } else if (_streq_icase(key, "cal_ref_dis_voltage_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cal_ref_dis_voltage_v = value_f;
    } else if (_streq_icase(key, "cal_ref_chg_current_a")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cal_ref_chg_current_a = value_f;
    } else if (_streq_icase(key, "cal_ref_chg_voltage_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cal_ref_chg_voltage_v = value_f;
    } else if (_streq_icase(key, "cal_ref_cell1_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cal_ref_cell1_v = value_f;
    } else if (_streq_icase(key, "cal_ref_cell2_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cal_ref_cell2_v = value_f;
    } else if (_streq_icase(key, "cal_ref_cell3_v")) {
        if (!_parse_float(value_text, &value_f)) return false;
        cfg.cal_ref_cell3_v = value_f;
    } else if (_streq_icase(key, "sound") || _streq_icase(key, "buzzer_preset")) {
        if (_streq_icase(value_text, "full")) preset = BUZ_PRESET_FULL;
        else if (_streq_icase(value_text, "minimal")) preset = BUZ_PRESET_MINIMAL;
        else if (_streq_icase(value_text, "silent")) preset = BUZ_PRESET_SILENT;
        else return false;
        cfg.buzzer_preset = preset;
        cfg.buzzer_en = (preset != BUZ_PRESET_SILENT) ? 1u : 0u;
    } else if (_streq_icase(key, "esp_mode")) {
        if (!_parse_esp_mode_value(value_text, &esp_mode)) return false;
        cfg.esp_mode = esp_mode;
    } else if (_streq_icase(key, "time")) {
        /* SET time <epoch> — sync real-time clock from ESP/NTP */
        uint32_t epoch = (uint32_t)strtoul(value_text, NULL, 10);
        if (epoch > 1700000000u) {
            log_set_epoch(epoch);
            return true;
        }
        return false;
    } else {
        return false;
    }

    return esp->store_settings(&cfg, reconfigure_sensors);
}

static bool _port_from_text(const char *text, PortId *port_out) {
    if (!text || !port_out) return false;
    if (_streq_icase(text, "dc_out") || _streq_icase(text, "dc") || _streq_icase(text, "dcout")) {
        *port_out = PORT_DC_OUT;
        return true;
    }
    if (_streq_icase(text, "usb_pd") || _streq_icase(text, "usb-pd") || _streq_icase(text, "usb")) {
        *port_out = PORT_USB_PD;
        return true;
    }
    return false;
}

static bool _apply_port_set(EspManager *esp, const char *port_text, const char *state_text, const char **detail_out) {
    PortId port;
    bool target_on;
    if (detail_out) *detail_out = "invalid";
    if (!esp || !esp->pwr || !port_text || !state_text) return false;
    if (!_port_from_text(port_text, &port)) {
        if (detail_out) *detail_out = "unknown_port";
        return false;
    }
    if (_streq_icase(state_text, "toggle")) {
        pwr_user_toggle(esp->pwr, port);
    } else {
        if (_streq_icase(state_text, "on") || _streq_icase(state_text, "1")) target_on = true;
        else if (_streq_icase(state_text, "off") || _streq_icase(state_text, "0")) target_on = false;
        else {
            if (detail_out) *detail_out = "unknown_state";
            return false;
        }
        pwr_user_set(esp->pwr, port, target_on);
    }
    if (detail_out) {
        *detail_out = (port == PORT_DC_OUT) ? "dc_out" : "usb_pd";
    }
    return true;
}

static void _process_line(EspManager *esp, char *line, const BatSnapshot *snap, uint32_t now_ms) {
    char *cmd;
    char *args = NULL;
    char *arg1;
    char *arg2;
    char *arg3;
    char detail[80];
    char raw_line[ESP_RX_LINE_MAX];
    if (!esp || !line) return;

    snprintf(raw_line, sizeof(raw_line), "%s", line);

    while (*line == ' ') ++line;
    if (*line == '\0') return;

    cmd = line;
    args = strchr(line, ' ');
    if (args) {
        *args++ = '\0';
        while (*args == ' ') ++args;
        if (*args == '\0') args = NULL;
    }
    arg1 = args ? strtok(args, " ") : NULL;
    arg2 = arg1 ? strtok(NULL, " ") : NULL;
    arg3 = arg2 ? strtok(NULL, " ") : NULL;

    esp->rx_frames++;
    esp->last_rx_ms = now_ms;
    esp->link_up = true;
    if (_status_is_transient(esp->last_status)) {
        _status_set(esp, "ONLINE");
    }

    if (esp->ota.session_active && !_streq_icase(cmd, "OTA")) {
        printf("[OTA RX] ignore cmd='%s' while OTA session active\n", cmd);
        return;
    }

    if (_streq_icase(cmd, "PING")) {
        _send_ack(esp, "ping", esp_mode_name(esp_mode_get(esp)), true);
    } else if (_streq_icase(cmd, "HELLO") || _streq_icase(cmd, "READY")) {
        _send_hello(esp);
        if (snap) _send_telemetry(esp, snap, now_ms);
    } else if (_streq_icase(cmd, "STATUS")) {
        _status_set(esp, (args && args[0]) ? args : "ONLINE");
    } else if (_streq_icase(cmd, "HELP")) {
        _send_help(esp);
    } else if (_streq_icase(cmd, "GET")) {
        if (!arg1) {
            _send_error(esp, "missing_get_target");
        } else if (_streq_icase(arg1, "TELEMETRY")) {
            if (snap) _send_telemetry(esp, snap, now_ms);
            else _send_error(esp, "telemetry_unavailable");
        } else if (_streq_icase(arg1, "STATS")) {
            _send_stats(esp);
        } else if (_streq_icase(arg1, "SETTINGS")) {
            _send_settings(esp);
        } else if (_streq_icase(arg1, "PORTS")) {
            _send_ports(esp);
        } else if (_streq_icase(arg1, "OTA")) {
            _send_ota_status(esp);
        } else if (_streq_icase(arg1, "LOG")) {
            uint32_t start = 0u;
            uint32_t count = 8u;
            if (arg2 && !_parse_uint32(arg2, &start)) {
                _send_error(esp, "bad_log_start");
                return;
            }
            if (arg3 && !_parse_uint32(arg3, &count)) {
                _send_error(esp, "bad_log_count");
                return;
            }
            _send_log_slice(esp, start, count);
        } else {
            _send_error(esp, "unknown_get_target");
        }
    } else if (_streq_icase(cmd, "OTA")) {
        printf("[OTA RX] raw='%s' arg1='%s' arg2='%s' arg3='%s' mode=%s safe=%u\n",
               raw_line,
               arg1 ? arg1 : "",
               arg2 ? arg2 : "",
               arg3 ? arg3 : "",
               esp_mode_name(esp_mode_get(esp)),
               esp->pico_ota_ready ? 1u : 0u);
        if (!arg1) {
            printf("[OTA RX] reject: missing action\n");
            _send_error(esp, "missing_ota_action");
            return;
        }
        if (esp_mode_get(esp) != ESP_MODE_OTA) {
            printf("[OTA RX] reject: bridge not in OTA mode\n");
            _send_ack(esp, "ota", "bridge-not-in-ota-mode", false);
            return;
        }

        if (_streq_icase(arg1, "BEGIN")) {
            uint32_t image_size = 0u;
            if (!_parse_u32_auto(arg2, &image_size)) {
                printf("[OTA RX] bad size token='%s' raw='%s'\n",
                       arg2 ? arg2 : "(null)",
                       raw_line);
                _send_error(esp, "bad_ota_size");
                return;
            }
            if (!pico_ota_is_supported()) {
                printf("[OTA RX] reject: slot build unsupported\n");
                _send_ack(esp, "ota_begin", "slot-build-required", false);
                return;
            }
            if (!esp->pico_ota_ready) {
                printf("[OTA RX] reject: pico not in OTA SAFE\n");
                _send_ack(esp, "ota_begin", "pico-not-in-ota-safe-mode", false);
                return;
            }
            if (!esp->pico_ota_ready &&
                (!snap || isnan(snap->voltage) || isinf(snap->voltage) ||
                 snap->voltage < settings_get()->vbat_warn_v)) {
                printf("[OTA RX] reject: battery low or invalid, vbat=%.3f warn=%.3f\n",
                       snap ? snap->voltage : 0.0f,
                       settings_get()->vbat_warn_v);
                _send_ack(esp, "ota_begin", "battery-too-low", false);
                return;
            }
            printf("[OTA RX] begin parsed size=%lu version='%s' target=%s\n",
                   (unsigned long)image_size,
                   arg3 ? arg3 : "",
                   pico_ota_slot_name(pico_ota_target_slot()));
            if (pico_ota_begin(&esp->ota, image_size, arg3, detail, sizeof(detail))) {
                printf("[OTA RX] begin ok detail='%s'\n", detail);
                _status_set(esp, "PICO OTA RX");
                _send_ack(esp, "ota_begin", detail, true);
            } else {
                printf("[OTA RX] begin fail detail='%s'\n", detail);
                _status_set(esp, "PICO OTA FAIL");
                _send_ack(esp, "ota_begin", detail, false);
            }
        } else if (_streq_icase(arg1, "CHUNK")) {
            uint8_t chunk[PICO_OTA_UART_CHUNK_MAX];
            uint32_t offset = 0u;
            size_t chunk_len = 0u;
            if (!_parse_u32_auto(arg2, &offset)) {
                printf("[OTA RX] bad offset token='%s'\n", arg2 ? arg2 : "(null)");
                _send_error(esp, "bad_ota_offset");
                return;
            }
            if (!arg3 || !_parse_hex_blob(arg3, chunk, sizeof(chunk), &chunk_len)) {
                printf("[OTA RX] bad chunk len/token offset=%lu\n", (unsigned long)offset);
                _send_error(esp, "bad_ota_chunk");
                return;
            }
            if (pico_ota_write_chunk(&esp->ota, offset, chunk, chunk_len, detail, sizeof(detail))) {
                _status_set(esp, "PICO OTA RX");
                _send_ack(esp, "ota_chunk", detail, true);
            } else {
                printf("[OTA RX] chunk fail offset=%lu len=%u detail='%s'\n",
                       (unsigned long)offset,
                       (unsigned)chunk_len,
                       detail);
                _status_set(esp, "PICO OTA FAIL");
                _send_ack(esp, "ota_chunk", detail, false);
            }
        } else if (_streq_icase(arg1, "END")) {
            uint32_t crc32 = 0u;
            if (!_parse_u32_auto(arg2, &crc32)) {
                printf("[OTA RX] bad crc token='%s'\n", arg2 ? arg2 : "(null)");
                _send_error(esp, "bad_ota_crc32");
                return;
            }
            printf("[OTA RX] finalize crc=%08lX written=%lu size=%lu\n",
                   (unsigned long)crc32,
                   (unsigned long)esp->ota.bytes_written,
                   (unsigned long)esp->ota.image_size);
            if (pico_ota_finalize(&esp->ota, crc32, now_ms, detail, sizeof(detail))) {
                printf("[OTA RX] finalize ok detail='%s'\n", detail);
                _status_set(esp, "PICO OTA APPLY");
                _send_ack(esp, "ota_end", detail, true);
            } else {
                printf("[OTA RX] finalize fail detail='%s'\n", detail);
                _status_set(esp, "PICO OTA FAIL");
                _send_ack(esp, "ota_end", detail, false);
            }
        } else if (_streq_icase(arg1, "ABORT")) {
            printf("[OTA RX] abort reason='%s'\n", arg2 ? arg2 : "aborted");
            pico_ota_abort(&esp->ota, arg2 ? arg2 : "aborted");
            _status_set(esp, "PICO OTA ABORT");
            _send_ack(esp, "ota_abort", arg2 ? arg2 : "aborted", true);
        } else {
            _send_error(esp, "unknown_ota_action");
        }
    } else if (_streq_icase(cmd, "SET")) {
        bool ok;
        if (!arg1 || !arg2) {
            _send_error(esp, "missing_set_args");
            return;
        }
        ok = _apply_setting(esp, arg1, arg2);
        _send_ack(esp, "set", arg1, ok);
    } else if (_streq_icase(cmd, "ACTION")) {
        if (!arg1) {
            _send_error(esp, "missing_action");
        } else if (_streq_icase(arg1, "LOG_RESET")) {
            if (esp->logger) {
                log_reset(esp->logger);
                _send_ack(esp, "action", "log_reset", true);
            } else {
                _send_ack(esp, "action", "log_reset", false);
            }
        } else if (_streq_icase(arg1, "STATS_RESET")) {
            if (esp->stats) {
                stats_reset(esp->stats);
                _send_ack(esp, "action", "stats_reset", true);
            } else {
                _send_ack(esp, "action", "stats_reset", false);
            }
        } else if (_streq_icase(arg1, "PORT_SET")) {
            const char *detail = "port_set";
            const bool ok = _apply_port_set(esp, arg2, arg3, &detail);
            _send_ack(esp, "action", detail, ok);
        } else if (_streq_icase(arg1, "SHUTDOWN")) {
            esp->shutdown_requested = true;
            _send_ack(esp, "action", "shutdown", true);
        } else {
            _send_error(esp, "unknown_action");
        }
    } else {
        esp->cmd_errors++;
        _send_error(esp, "unknown_command");
    }
}

static void _poll_rx(EspManager *esp, const BatSnapshot *snap, uint32_t now_ms) {
    while (esp && esp->uart_ready && uart_is_readable(uart1)) {
        char ch = (char)uart_getc(uart1);
        if (ch == '\r' || ch == '\n') {
            if (esp->rx_len > 0u) {
                esp->rx_line[esp->rx_len] = '\0';
                _process_line(esp, esp->rx_line, snap, now_ms);
                esp->rx_len = 0u;
            }
            continue;
        }
        if ((unsigned char)ch < 32u || (unsigned char)ch > 126u) continue;
        if (esp->rx_len + 1u >= sizeof(esp->rx_line)) {
            esp->rx_len = 0u;
            esp->cmd_errors++;
            _status_set(esp, "RX OVERFLOW");
            printf("[ESP RX] overflow, line dropped\n");
            continue;
        }
        esp->rx_line[esp->rx_len++] = ch;
    }
}

void esp_init(EspManager *esp,
              BmsLogger *logger,
              BmsStats *stats,
              PowerControl *pwr,
              EspStoreSettingsFn store_settings) {
    bool keep_power;
    if (!esp) return;
    memset(esp, 0, sizeof(*esp));
    esp->logger = logger;
    esp->stats = stats;
    esp->pwr = pwr;
    esp->store_settings = store_settings;
    pico_ota_init(&esp->ota);
    keep_power = _boot_keep_esp_requested();

    gpio_init(GPIO_ESP_EN);
    gpio_set_dir(GPIO_ESP_EN, GPIO_OUT);
    gpio_put(GPIO_ESP_EN, keep_power ? ESP_EN_ON : ESP_EN_OFF);

    uart_init(uart1, ESP_UART_BAUD_HZ);
    gpio_set_function(ESP_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(ESP_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(uart1, false, false);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart1, true);

    esp->uart_ready = true;
    esp->mode = ESP_MODE_OFF;
    esp->boot_ready = false;
    esp->pico_ota_ready = false;
    esp->powered = keep_power;
    esp->preserve_power = keep_power;
    _status_set(esp, keep_power ? "HANDOFF BOOT" : "OFF");
    esp_apply_settings(esp, settings_get());
}

void esp_set_boot_ready(EspManager *esp, bool ready) {
    uint32_t now_ms;
    if (!esp) return;

    now_ms = to_ms_since_boot(get_absolute_time());
    esp->boot_ready = ready;
    esp->boot_release_ms = ready ? (now_ms + ESP_POST_BOOT_DELAY_MS) : 0u;

    if (!ready) {
        _power_set(esp, false, now_ms);
    }

    esp_apply_settings(esp, settings_get());
    printf("[ESP] boot gate=%s release_in=%lums\n",
           ready ? "OPEN" : "LOCKED",
           ready ? (unsigned long)ESP_POST_BOOT_DELAY_MS : 0ul);
}

void esp_set_pico_ota_ready(EspManager *esp, bool ready) {
    if (!esp) return;
    esp->pico_ota_ready = ready;
}

void esp_apply_settings(EspManager *esp, const SystemSettings *cfg) {
    bool want_power;
    bool power_allowed;
    uint32_t now_ms;
    EspMode next_mode;
    if (!esp || !cfg) return;

    now_ms = to_ms_since_boot(get_absolute_time());
    next_mode = (cfg->esp_mode < ESP_MODE_COUNT) ? (EspMode)cfg->esp_mode : ESP_MODE_OFF;
    power_allowed = esp->boot_ready && (now_ms >= esp->boot_release_ms);
    want_power = power_allowed && (next_mode != ESP_MODE_OFF);
    if (esp->preserve_power && next_mode != ESP_MODE_OFF) {
        want_power = true;
    }

    esp->mode = (uint8_t)next_mode;
    _power_set(esp, want_power, now_ms);
    if (esp->preserve_power && next_mode == ESP_MODE_OFF) {
        esp->preserve_power = false;
        _boot_keep_esp_clear();
    }
    if (esp->preserve_power && power_allowed) {
        esp->preserve_power = false;
        _boot_keep_esp_clear();
    }

    if (!want_power) {
        if (next_mode == ESP_MODE_OFF) {
            _status_set(esp, "OFF");
        } else if (!esp->boot_ready) {
            _status_set(esp, "BOOT HOLD");
        } else {
            _status_set(esp, "BOOT WAIT");
        }
    } else if (next_mode == ESP_MODE_OTA) {
        _status_set(esp, "OTA BOOT");
    } else {
        _status_set(esp, "WEB BOOT");
    }

    printf("[ESP] mode=%s power=%s UART1 GP%u/GP%u EN=GP%u\n",
           esp_mode_name(next_mode),
           want_power ? "ON" : "OFF",
           ESP_UART_TX_PIN,
           ESP_UART_RX_PIN,
           GPIO_ESP_EN);
}

void esp_update(EspManager *esp, const BatSnapshot *snap, uint32_t now_ms) {
    bool ota_busy;
    if (!esp || !esp->uart_ready) return;

    if (!esp->powered &&
        esp->boot_ready &&
        esp->mode != ESP_MODE_OFF &&
        now_ms >= esp->boot_release_ms) {
        _power_set(esp, true, now_ms);
        _status_set(esp, (esp->mode == ESP_MODE_OTA) ? "OTA BOOT" : "WEB BOOT");
    }

    _poll_rx(esp, snap, now_ms);

    if (!esp->powered || esp->mode == ESP_MODE_OFF) return;
    if (now_ms < esp->ready_at_ms) return;
    ota_busy = pico_ota_is_busy(&esp->ota) || esp->ota.reboot_pending;

    if (esp->link_up && (now_ms - esp->last_rx_ms) > ESP_LINK_TIMEOUT_MS) {
        esp->link_up = false;
        _status_set(esp, "WAIT LINK");
    }

    if (!ota_busy && (now_ms - esp->last_hello_ms) >= ESP_HELLO_INTERVAL_MS) {
        if (!esp->link_up || _starts_with_icase(esp->last_status, "WEB") || _starts_with_icase(esp->last_status, "OTA")) {
            _send_hello(esp);
        }
        esp->last_hello_ms = now_ms;
    }

    if (!ota_busy && snap && (now_ms - esp->last_telemetry_ms) >= ESP_TELEMETRY_INTERVAL_MS) {
        _send_telemetry(esp, snap, now_ms);
        esp->last_telemetry_ms = now_ms;
    }

    if (pico_ota_should_reboot(&esp->ota, now_ms)) {
        printf("[OTA] rebooting into loader for %s\n", pico_ota_slot_name(esp->ota.target_slot));
        _status_set(esp, "PICO OTA REBOOT");
        watchdog_hw->scratch[BOOTCTL_WATCHDOG_HANDOFF_MAGIC_SCRATCH] = BOOTCTL_WATCHDOG_HANDOFF_MAGIC;
        watchdog_hw->scratch[BOOTCTL_WATCHDOG_HANDOFF_SLOT_SCRATCH] = (uint32_t)esp->ota.target_slot;
        watchdog_hw->scratch[BOOTCTL_WATCHDOG_KEEP_ESP_SCRATCH] = BOOTCTL_WATCHDOG_KEEP_ESP_MAGIC;
        sleep_ms(20);
        watchdog_reboot(0u, 0u, 50u);
        while (1) {
            tight_loop_contents();
        }
    }
}

EspMode esp_mode_get(const EspManager *esp) {
    if (!esp || esp->mode >= ESP_MODE_COUNT) return ESP_MODE_OFF;
    return (EspMode)esp->mode;
}

bool esp_is_powered(const EspManager *esp) {
    return esp ? esp->powered : false;
}

bool esp_is_link_up(const EspManager *esp) {
    return esp ? esp->link_up : false;
}

const char *esp_mode_name(EspMode mode) {
    switch (mode) {
        case ESP_MODE_WEB: return "WEB";
        case ESP_MODE_OTA: return "OTA";
        case ESP_MODE_OFF:
        default:           return "OFF";
    }
}

const char *esp_status_text(const EspManager *esp) {
    if (!esp) return "OFF";
    return esp->last_status[0] ? esp->last_status : "OFF";
}
