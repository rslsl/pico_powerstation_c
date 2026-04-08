#pragma once
// ============================================================
// bms/bms_logger.h - Append-only flash event logger (v2)
// ============================================================
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_EVENT_SIZE          32
#define LOG_PAGE_SIZE           256
#define LOG_RECORDS_PER_PAGE    (LOG_PAGE_SIZE / LOG_EVENT_SIZE)   // 8
#define LOG_RECORDS_PER_SECTOR  (4096 / LOG_EVENT_SIZE)            // 128
#define LOG_HDR_SAVE_PERIOD     32

typedef enum {
    LOG_CAT_SYSTEM = 0,
    LOG_CAT_POWER  = 1,
    LOG_CAT_PROT   = 2,
    LOG_CAT_THERM  = 3,
    LOG_CAT_DATA   = 4,
} LogCategory;

typedef enum {
    LOG_BOOT = 0,
    LOG_SAVE_OK,
    LOG_SAVE_SKIP,
    LOG_BROWNOUT_ENTER,
    LOG_BROWNOUT_EXIT,
    LOG_SAFE_MODE_ENTER,
    LOG_SAFE_MODE_EXIT,

    LOG_CHARGER_PRESENT,
    LOG_CHARGER_LOST,
    LOG_CHARGE_START,
    LOG_CHARGE_END,
    LOG_DISCHARGE_START,
    LOG_DISCHARGE_END,
    LOG_PORT_CHANGED,

    LOG_ALARM_ENTER,
    LOG_ALARM_EXIT,
    LOG_ALARM_LEVEL,
    LOG_OCP_EVENT,
    LOG_SOC_WARN,
    LOG_TEMP_WARN,

    LOG_FAN_ON,
    LOG_FAN_OFF,
    LOG_FAN_BLOCKED,

    LOG_SENSOR_FAULT,
    LOG_SENSOR_RECOVERED,
    LOG_I2C_RECOVERY,
} LogEventTypeV2;

typedef enum {
    LOG_REASON_NONE = 0,
    LOG_REASON_STARTUP_HOLDOFF,
    LOG_REASON_SOC_SETTLING,
    LOG_REASON_PACK_STALE,
    LOG_REASON_CELLS_STALE,
    LOG_REASON_BROWNOUT,
    LOG_REASON_INVALID_SOC,
    LOG_REASON_INVALID_SOH,
    LOG_REASON_INVALID_R0,
    LOG_REASON_SAFE_MODE,
    LOG_REASON_NO_SENSOR,
    LOG_REASON_USER,
    LOG_REASON_AUTO,
    LOG_REASON_SOC_UNSTABLE,
} LogReasonCode;

typedef struct __attribute__((packed)) {
    uint32_t timestamp_s;   /* uptime_s if < 1700000000, epoch if >= */
    uint8_t  type;          /* LogEventTypeV2 */
    uint8_t  category;      /* LogCategory */
    uint8_t  soc_pct;
    uint8_t  temp_bat;
    float    voltage;
    float    current;
    float    value1;        /* event-specific numeric value */
    uint16_t code;          /* reason / subtype (LogReasonCode or sensor code) */
    uint8_t  aux;           /* small state / port id / level / session marker */
    uint8_t  reserved;
    uint32_t flags;         /* alarm bits or other bitmasks */
    uint32_t crc;
} LogEvent;

_Static_assert(sizeof(LogEvent) == LOG_EVENT_SIZE, "LogEvent size mismatch");

typedef struct {
    uint32_t write_idx;
    uint32_t total_events;
    uint32_t _hdr_seq;
    uint8_t  _hdr_slot;
    uint8_t  _page_valid_count;
    bool     _page_dirty;
    bool     _page_loaded;
    uint32_t _page_base_write_idx;
    uint8_t  _page_buf[LOG_PAGE_SIZE];
} BmsLogger;

void     log_init(BmsLogger *l);
void     log_write(BmsLogger *l, const LogEvent *ev);
bool     log_read(const BmsLogger *l, uint32_t idx, LogEvent *ev_out);
uint32_t log_count(const BmsLogger *l);
void     log_flush_header(BmsLogger *l);
void     log_reset(BmsLogger *l);

void log_set_brownout(bool state);
void log_set_epoch(uint32_t epoch);
bool log_has_epoch(void);

// Base writer
void log_write_ex(BmsLogger *l,
                  uint8_t type,
                  uint8_t category,
                  uint8_t soc_pct,
                  uint8_t temp_bat,
                  float voltage,
                  float current,
                  float value1,
                  uint16_t code,
                  uint8_t aux,
                  uint32_t flags);

// Typed wrappers
void log_boot(BmsLogger *l, float soc, float v);
void log_save_ok(BmsLogger *l, float soc, float v);
void log_save_skip(BmsLogger *l, uint16_t reason, float soc, float v);

void log_brownout_enter(BmsLogger *l, float v);
void log_brownout_exit(BmsLogger *l, float v);

void log_safe_mode_enter(BmsLogger *l, uint16_t reason, float soc, float v);
void log_safe_mode_exit(BmsLogger *l, float soc, float v);

/* Returns current epoch seconds if set, otherwise 0 */
uint32_t log_now_epoch(void);

void log_charger_present(BmsLogger *l, float v, float i);
void log_charger_lost(BmsLogger *l, float v);

void log_charge_start(BmsLogger *l, float soc, float v, float current);
void log_charge_end(BmsLogger *l, float soc, float wh);
void log_discharge_start(BmsLogger *l, float soc, float v, float current);
void log_discharge_end(BmsLogger *l, float soc, float wh, float duration_h);

void log_port_changed(BmsLogger *l, uint8_t port_id, bool on, float v, float current);

void log_alarm_enter(BmsLogger *l, uint32_t flags, float soc, float v, float t);
void log_alarm_exit(BmsLogger *l, uint32_t cleared_flags, float soc, float v, float t);
void log_alarm_level(BmsLogger *l, uint16_t code, uint32_t flags, float soc, float v, float t);

void log_fan_on(BmsLogger *l, float tbat, float tinv);
void log_fan_off(BmsLogger *l, float tbat, float tinv);
void log_fan_blocked(BmsLogger *l, uint16_t reason);

void log_sensor_fault(BmsLogger *l, uint16_t sensor_code, uint32_t valid_mask);
void log_sensor_recovered(BmsLogger *l, uint16_t sensor_code, uint32_t valid_mask);
void log_i2c_recovery(BmsLogger *l, uint32_t fail_count);
