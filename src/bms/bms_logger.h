#pragma once
// ============================================================
// bms/bms_logger.h — Event ring-buffer logger on Flash NVM
//
// v2.1 fixes:
//   #6  — CRC32 per record + dual alternating headers (atomic)
//   #10 — brown-out guard API
//   #12 — log_set_brownout() blocks writes при низькій напрузі
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define LOG_EVENT_SIZE          32
#define LOG_RECORDS_PER_SECTOR  (4096 / LOG_EVENT_SIZE)   // 128
#define LOG_HDR_SAVE_PERIOD     32    // зберегти header кожні N записів

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

// 32-byte packed event record (включно з CRC)
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
    uint8_t  _pad[4];     // padding to reach 32 bytes
    uint32_t crc;         // CRC32 перших 28 байт; поле crc = байти 28-31
} LogEvent;               // = 32 bytes: 28 + 4

// Перевірка розміру — упаде при компіляції якщо struct не 32 байти
_Static_assert(sizeof(LogEvent) == LOG_EVENT_SIZE,
               "LogEvent size mismatch");

typedef struct {
    uint32_t write_idx;
    uint32_t total_events;
    // Internal
    uint32_t _hdr_seq;
    uint8_t  _hdr_slot;   // 0 або 1
} BmsLogger;

void     log_init(BmsLogger *l);
void     log_write(BmsLogger *l, const LogEvent *ev);
bool     log_read(const BmsLogger *l, uint32_t idx, LogEvent *ev_out);
uint32_t log_count(const BmsLogger *l);
void     log_flush_header(BmsLogger *l);
void     log_reset(BmsLogger *l);

// Brown-out API: викликати з protection.c при V < VBAT_BROWNOUT
void     log_set_brownout(bool state);

// Helpers
void log_boot      (BmsLogger *l, float soc, float v);
void log_charge_start(BmsLogger *l, float soc, float v, float current);
void log_discharge_end(BmsLogger *l, float soc, float wh);
void log_alarm     (BmsLogger *l, uint32_t alarm_flags,
                    float soc, float v, float t);
void log_charge_end(BmsLogger *l, float soc, float wh);
