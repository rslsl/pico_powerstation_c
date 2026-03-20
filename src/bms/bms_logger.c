// ============================================================
// bms/bms_logger.c — Flash ring-buffer event log
//
// v4.0: refactored to use flash_nvm backend (P3.14)
//   All flash erase/program via nvm_write_sector() which
//   handles multicore lockout + irq disable internally (P0.2).
// ============================================================
#include "bms_logger.h"
#include "flash_nvm.h"
#include "../config.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#define LOG_CAPACITY  ((FLASH_LOG_SECTORS - 2) * LOG_RECORDS_PER_SECTOR)
#define LOG_MAGIC     0x4C4F4731u

#define LOG_HDR_SECTOR_A  0
#define LOG_HDR_SECTOR_B  1
#define LOG_DATA_START    2

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;
    uint32_t write_idx;
    uint32_t total_events;
    uint32_t crc;
    uint8_t  _pad[4076];
} LogHeader;
_Static_assert(sizeof(LogHeader) == 4096, "LogHeader must be 4096");

static uint32_t _hdr_crc(const LogHeader *h) {
    return nvm_crc32(h, offsetof(LogHeader, crc));
}

static void _header_write(uint32_t slot, uint32_t seq,
                           uint32_t write_idx, uint32_t total) {
    static uint8_t hdr_buf[FLASH_SECTOR_SIZE];
    memset(hdr_buf, 0xFF, sizeof(hdr_buf));
    LogHeader *h = (LogHeader*)hdr_buf;
    h->magic        = LOG_MAGIC;
    h->seq          = seq;
    h->write_idx    = write_idx;
    h->total_events = total;
    h->crc          = _hdr_crc(h);
    // P3.14: use nvm_write_sector (multicore lockout inside)
    nvm_write_sector(FLASH_LOG_OFFSET + slot * FLASH_SECTOR_SIZE, hdr_buf);
}

static bool _header_load(uint32_t *write_idx, uint32_t *total,
                          uint32_t *seq_out, uint8_t *active_slot) {
    LogHeader hA, hB;
    nvm_read(FLASH_LOG_OFFSET + 0 * FLASH_SECTOR_SIZE, &hA, sizeof(hA));
    nvm_read(FLASH_LOG_OFFSET + 1 * FLASH_SECTOR_SIZE, &hB, sizeof(hB));
    bool vA = (hA.magic == LOG_MAGIC) && (_hdr_crc(&hA) == hA.crc);
    bool vB = (hB.magic == LOG_MAGIC) && (_hdr_crc(&hB) == hB.crc);
    if (!vA && !vB) return false;
    const LogHeader *best;
    if      (vA && vB) { best = (hA.seq >= hB.seq) ? &hA : &hB; }
    else if (vA)       { best = &hA; }
    else               { best = &hB; }
    *write_idx   = best->write_idx;
    *total       = best->total_events;
    *seq_out     = best->seq;
    *active_slot = (best == &hA) ? 0 : 1;
    return true;
}

static volatile bool g_brownout = false;
void log_set_brownout(bool state) {
    g_brownout = state;
    if (state) printf("[LOG] brownout — writes disabled\n");
}

void log_init(BmsLogger *l) {
    l->_hdr_slot = 0; l->_hdr_seq = 0;
    if (!_header_load(&l->write_idx, &l->total_events,
                      &l->_hdr_seq, &l->_hdr_slot)) {
        l->write_idx = 0; l->total_events = 0;
        printf("[LOG] fresh log\n");
    } else {
        printf("[LOG] loaded: widx=%lu total=%lu seq=%lu slot=%d\n",
               (unsigned long)l->write_idx,
               (unsigned long)l->total_events,
               (unsigned long)l->_hdr_seq, l->_hdr_slot);
    }
}

void log_write(BmsLogger *l, const LogEvent *ev) {
    if (g_brownout) return;
    LogEvent ev_crc = *ev;
    ev_crc.crc = 0;
    ev_crc.crc = nvm_crc32(&ev_crc, sizeof(LogEvent));   // P3.14: use nvm_crc32

    uint32_t ring_idx = l->write_idx % (uint32_t)LOG_CAPACITY;
    uint32_t d_sector = LOG_DATA_START + (ring_idx / LOG_RECORDS_PER_SECTOR);
    uint32_t rec_in_s = ring_idx % LOG_RECORDS_PER_SECTOR;
    uint32_t fl_off   = FLASH_LOG_OFFSET + d_sector * FLASH_SECTOR_SIZE;

    static uint8_t sector_buf[FLASH_SECTOR_SIZE];
    nvm_read(fl_off, sector_buf, FLASH_SECTOR_SIZE);
    memcpy(sector_buf + rec_in_s * LOG_EVENT_SIZE, &ev_crc, LOG_EVENT_SIZE);
    nvm_write_sector(fl_off, sector_buf);   // P3.14: lockout inside

    l->write_idx++;
    l->total_events++;

    if (l->write_idx % LOG_HDR_SAVE_PERIOD == 0) {
        uint8_t next = (l->_hdr_slot == 0) ? 1 : 0;
        l->_hdr_seq++;
        _header_write(next, l->_hdr_seq, l->write_idx, l->total_events);
        l->_hdr_slot = next;
    }
}

bool log_read(const BmsLogger *l, uint32_t idx, LogEvent *ev_out) {
    if (idx >= l->total_events) return false;
    uint32_t ring_idx = idx % (uint32_t)LOG_CAPACITY;
    uint32_t d_sector = LOG_DATA_START + (ring_idx / LOG_RECORDS_PER_SECTOR);
    uint32_t rec_in_s = ring_idx % LOG_RECORDS_PER_SECTOR;
    uint32_t off = FLASH_LOG_OFFSET + d_sector * FLASH_SECTOR_SIZE
                   + rec_in_s * LOG_EVENT_SIZE;
    nvm_read(off, ev_out, LOG_EVENT_SIZE);
    uint32_t stored = ev_out->crc;
    ev_out->crc = 0;
    uint32_t calc = nvm_crc32(ev_out, sizeof(LogEvent));
    ev_out->crc = stored;
    if (calc != stored) {
        printf("[LOG] CRC err idx=%lu\n", (unsigned long)idx);
        return false;
    }
    return true;
}

uint32_t log_count(const BmsLogger *l) {
    uint32_t cap = (uint32_t)LOG_CAPACITY;
    return l->total_events < cap ? l->total_events : cap;
}

void log_flush_header(BmsLogger *l) {
    if (g_brownout) return;
    uint8_t next = (l->_hdr_slot == 0) ? 1 : 0;
    l->_hdr_seq++;
    _header_write(next, l->_hdr_seq, l->write_idx, l->total_events);
    l->_hdr_slot = next;
}

static uint32_t _ts(void) {
    return (uint32_t)(to_ms_since_boot(get_absolute_time()) / 1000u);
}
void log_boot(BmsLogger *l, float soc, float v) {
    LogEvent ev = {0}; ev.timestamp_s = _ts(); ev.type = LOG_BOOT;
    ev.soc_pct = (uint8_t)soc; ev.voltage = v; log_write(l, &ev);
}
void log_charge_start(BmsLogger *l, float soc, float v, float current) {
    LogEvent ev = {0}; ev.timestamp_s = _ts(); ev.type = LOG_CHARGE_START;
    ev.soc_pct = (uint8_t)soc; ev.voltage = v; ev.current = current; log_write(l, &ev);
}
void log_discharge_end(BmsLogger *l, float soc, float wh) {
    LogEvent ev = {0}; ev.timestamp_s = _ts(); ev.type = LOG_DISCHARGE_END;
    ev.soc_pct = (uint8_t)soc; ev.param = wh; log_write(l, &ev);
}
void log_alarm(BmsLogger *l, uint32_t alarms, float soc, float v, float t) {
    LogEvent ev = {0}; ev.timestamp_s = _ts(); ev.type = LOG_ALARM;
    ev.soc_pct = (uint8_t)soc; ev.voltage = v;
    ev.temp_bat = (uint8_t)t; ev.alarm_flags = alarms; log_write(l, &ev);
}
void log_charge_end(BmsLogger *l, float soc, float wh) {
    LogEvent ev = {0}; ev.timestamp_s = _ts(); ev.type = LOG_CHARGE_END;
    ev.soc_pct = (uint8_t)soc; ev.param = wh; log_write(l, &ev);
}
