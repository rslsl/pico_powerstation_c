#pragma once
// ============================================================
// bms/bms_logger.h - Append-only flash event logger
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
    LOG_BOOT = 0,
    LOG_CHARGE_START,
    LOG_CHARGE_END,
    LOG_DISCHARGE_END,
    LOG_ALARM,
    LOG_SOC_WARN,
    LOG_TEMP_WARN,
    LOG_OCP,
    LOG_SAVE,
} LogEventType;

typedef struct __attribute__((packed)) {
    uint32_t timestamp_s;
    uint8_t  type;
    uint8_t  soc_pct;
    uint8_t  temp_bat;
    uint8_t  temp_inv;
    float    voltage;
    float    current;
    float    param;
    uint32_t alarm_flags;
    uint8_t  _pad[4];
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

void log_boot(BmsLogger *l, float soc, float v);
void log_charge_start(BmsLogger *l, float soc, float v, float current);
void log_discharge_end(BmsLogger *l, float soc, float wh);
void log_alarm(BmsLogger *l, uint32_t alarm_flags,
               float soc, float v, float t);
void log_charge_end(BmsLogger *l, float soc, float wh);
