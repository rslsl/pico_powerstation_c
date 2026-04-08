// ============================================================
// bms/bms_logger.c - Append-only flash ring-buffer event log
// ============================================================
#include "bms_logger.h"
#include "flash_nvm.h"
#include "../config.h"
#include "pico/stdlib.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LOG_CAPACITY  ((FLASH_LOG_SECTORS - 2) * LOG_RECORDS_PER_SECTOR)
#define LOG_MAGIC     0x4C4F4731u

#define LOG_HDR_SECTOR_A  0u
#define LOG_HDR_SECTOR_B  1u
#define LOG_DATA_START    2u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;
    uint32_t write_idx;
    uint32_t total_events;
    uint32_t crc;
} LogHeaderPrefix;

_Static_assert(sizeof(LogHeaderPrefix) <= LOG_PAGE_SIZE, "Log header must fit one page");

static volatile bool g_brownout = false;
static volatile uint32_t g_epoch_offset = 0;  /* epoch - uptime_s at SET TIME */

static bool _seq_is_newer(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0;
}

static uint32_t _hdr_crc(const LogHeaderPrefix *h) {
    return nvm_crc32(h, offsetof(LogHeaderPrefix, crc));
}

static uint32_t _ring_idx_from_abs(uint32_t abs_idx) {
    return abs_idx % (uint32_t)LOG_CAPACITY;
}

static uint32_t _sector_offset_from_ring(uint32_t ring_idx) {
    uint32_t data_sector = ring_idx / LOG_RECORDS_PER_SECTOR;
    return FLASH_LOG_OFFSET + (LOG_DATA_START + data_sector) * FLASH_SECTOR_SIZE;
}

static uint32_t _page_offset_from_ring(uint32_t ring_idx) {
    uint32_t sector_off = _sector_offset_from_ring(ring_idx);
    uint32_t page_in_sector = (ring_idx % LOG_RECORDS_PER_SECTOR) / LOG_RECORDS_PER_PAGE;
    return sector_off + page_in_sector * LOG_PAGE_SIZE;
}

static uint32_t _page_offset_from_abs(uint32_t abs_idx) {
    return _page_offset_from_ring(_ring_idx_from_abs(abs_idx));
}

static bool _header_load(uint32_t *write_idx, uint32_t *total,
                         uint32_t *seq_out, uint8_t *active_slot) {
    LogHeaderPrefix hA = {0};
    LogHeaderPrefix hB = {0};
    nvm_read(FLASH_LOG_OFFSET + LOG_HDR_SECTOR_A * FLASH_SECTOR_SIZE, &hA, sizeof(hA));
    nvm_read(FLASH_LOG_OFFSET + LOG_HDR_SECTOR_B * FLASH_SECTOR_SIZE, &hB, sizeof(hB));

    bool vA = (hA.magic == LOG_MAGIC) && (_hdr_crc(&hA) == hA.crc);
    bool vB = (hB.magic == LOG_MAGIC) && (_hdr_crc(&hB) == hB.crc);
    if (!vA && !vB) return false;

    const LogHeaderPrefix *best = NULL;
    if (vA && vB) {
        best = (_seq_is_newer(hA.seq, hB.seq) || hA.seq == hB.seq) ? &hA : &hB;
    } else if (vA) {
        best = &hA;
    } else {
        best = &hB;
    }

    *write_idx = best->write_idx;
    *total = best->total_events;
    *seq_out = best->seq;
    *active_slot = (best == &hA) ? 0u : 1u;
    return true;
}

static void _header_write(uint8_t slot, uint32_t seq,
                          uint32_t write_idx, uint32_t total) {
    uint8_t page_buf[LOG_PAGE_SIZE];
    LogHeaderPrefix *h = (LogHeaderPrefix *)page_buf;
    uint32_t off = FLASH_LOG_OFFSET + slot * FLASH_SECTOR_SIZE;

    memset(page_buf, 0xFF, sizeof(page_buf));
    h->magic = LOG_MAGIC;
    h->seq = seq;
    h->write_idx = write_idx;
    h->total_events = total;
    h->crc = _hdr_crc(h);

    nvm_erase_sector(off);
    nvm_program_page(off, page_buf);
}

static void _page_reset(BmsLogger *l) {
    l->_page_loaded = false;
    l->_page_dirty = false;
    l->_page_valid_count = 0u;
    l->_page_base_write_idx = 0u;
    memset(l->_page_buf, 0xFF, sizeof(l->_page_buf));
}

static bool _page_flush(BmsLogger *l) {
    if (!l || !l->_page_loaded || !l->_page_dirty) return true;
    nvm_program_page(_page_offset_from_abs(l->_page_base_write_idx), l->_page_buf);
    l->_page_dirty = false;
    return true;
}

static bool _page_attach(BmsLogger *l, uint32_t abs_write_idx) {
    uint32_t ring_idx = _ring_idx_from_abs(abs_write_idx);
    uint32_t slot_in_page = ring_idx % LOG_RECORDS_PER_PAGE;
    uint32_t rec_in_sector = ring_idx % LOG_RECORDS_PER_SECTOR;
    uint32_t page_base_write_idx = abs_write_idx - slot_in_page;

    if (l->_page_loaded && l->_page_base_write_idx == page_base_write_idx) {
        return true;
    }

    if (!_page_flush(l)) return false;

    _page_reset(l);
    l->_page_loaded = true;
    l->_page_base_write_idx = page_base_write_idx;
    l->_page_valid_count = (uint8_t)slot_in_page;

    if (rec_in_sector == 0u) {
        nvm_erase_sector(_sector_offset_from_ring(ring_idx));
        return true;
    }

    if (slot_in_page != 0u) {
        nvm_read(_page_offset_from_abs(abs_write_idx), l->_page_buf, LOG_PAGE_SIZE);
    }
    return true;
}

static bool _header_commit(BmsLogger *l) {
    uint8_t next = (l->_hdr_slot == 0u) ? 1u : 0u;
    l->_hdr_seq++;
    _header_write(next, l->_hdr_seq, l->write_idx, l->total_events);
    l->_hdr_slot = next;
    return true;
}

static bool _read_flash_event(uint32_t abs_idx, LogEvent *ev_out) {
    uint32_t ring_idx = _ring_idx_from_abs(abs_idx);
    uint32_t off = _page_offset_from_ring(ring_idx)
                 + (ring_idx % LOG_RECORDS_PER_PAGE) * LOG_EVENT_SIZE;
    nvm_read(off, ev_out, LOG_EVENT_SIZE);
    return true;
}

static bool _validate_event(LogEvent *ev_out, uint32_t idx) {
    uint32_t stored = ev_out->crc;
    ev_out->crc = 0u;
    uint32_t calc = nvm_crc32(ev_out, sizeof(LogEvent));
    ev_out->crc = stored;
    if (calc != stored) {
        printf("[LOG] CRC err idx=%lu\n", (unsigned long)idx);
        return false;
    }
    return true;
}

static uint8_t _u8_clamped(float v, float lo, float hi) {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (uint8_t)v;
}

void log_set_brownout(bool state) {
    g_brownout = state;
    if (state) printf("[LOG] brownout - writes disabled\n");
}

void log_set_epoch(uint32_t epoch) {
    uint32_t uptime = (uint32_t)(to_ms_since_boot(get_absolute_time()) / 1000u);
    g_epoch_offset = epoch - uptime;
    printf("[LOG] epoch synced: %lu (offset=%lu)\n",
           (unsigned long)epoch, (unsigned long)g_epoch_offset);
}

bool log_has_epoch(void) {
    return g_epoch_offset != 0;
}

void log_init(BmsLogger *l) {
    memset(l, 0, sizeof(*l));
    _page_reset(l);

    if (!_header_load(&l->write_idx, &l->total_events, &l->_hdr_seq, &l->_hdr_slot)) {
        l->write_idx = 0u;
        l->total_events = 0u;
        printf("[LOG] fresh log\n");
    } else {
        printf("[LOG] loaded: widx=%lu total=%lu seq=%lu slot=%u\n",
               (unsigned long)l->write_idx,
               (unsigned long)l->total_events,
               (unsigned long)l->_hdr_seq,
               (unsigned)l->_hdr_slot);
    }
}

void log_write(BmsLogger *l, const LogEvent *ev) {
    LogEvent ev_crc;
    uint32_t abs_idx;
    uint32_t slot_in_page;

    if (!l || !ev || g_brownout) return;

    ev_crc = *ev;
    ev_crc.seq = l->total_events;  /* monotonic event counter */
    ev_crc.crc = 0u;
    ev_crc.crc = nvm_crc32(&ev_crc, sizeof(LogEvent));

    abs_idx = l->write_idx;
    if (!_page_attach(l, abs_idx)) return;

    slot_in_page = _ring_idx_from_abs(abs_idx) % LOG_RECORDS_PER_PAGE;
    memcpy(l->_page_buf + slot_in_page * LOG_EVENT_SIZE, &ev_crc, LOG_EVENT_SIZE);
    if (l->_page_valid_count < (uint8_t)(slot_in_page + 1u)) {
        l->_page_valid_count = (uint8_t)(slot_in_page + 1u);
    }
    l->_page_dirty = true;

    l->write_idx++;
    l->total_events++;

    if (l->_page_valid_count >= LOG_RECORDS_PER_PAGE) {
        _page_flush(l);
    }
    if ((l->write_idx % LOG_HDR_SAVE_PERIOD) == 0u) {
        _page_flush(l);
        _header_commit(l);
    }
}

bool log_read(const BmsLogger *l, uint32_t idx, LogEvent *ev_out) {
    uint32_t cap = (uint32_t)LOG_CAPACITY;
    uint32_t available;
    uint32_t first_retained;
    uint32_t abs_idx;

    if (!l || !ev_out) return false;
    available = log_count(l);
    if (idx >= available) return false;

    first_retained = (l->total_events > cap) ? (l->total_events - cap) : 0u;
    abs_idx = first_retained + idx;

    if (l->_page_loaded &&
        abs_idx >= l->_page_base_write_idx &&
        abs_idx < (l->_page_base_write_idx + l->_page_valid_count)) {
        uint32_t slot = abs_idx - l->_page_base_write_idx;
        memcpy(ev_out, l->_page_buf + slot * LOG_EVENT_SIZE, LOG_EVENT_SIZE);
    } else {
        _read_flash_event(abs_idx, ev_out);
    }

    return _validate_event(ev_out, idx);
}

uint32_t log_count(const BmsLogger *l) {
    uint32_t cap = (uint32_t)LOG_CAPACITY;
    if (!l) return 0u;
    return (l->total_events < cap) ? l->total_events : cap;
}

void log_flush_header(BmsLogger *l) {
    if (!l || g_brownout) return;
    _page_flush(l);
    _header_commit(l);
}

void log_reset(BmsLogger *l) {
    if (!l || g_brownout) return;
    l->write_idx = 0u;
    l->total_events = 0u;
    _page_reset(l);
    _header_commit(l);
    printf("[LOG] reset\n");
}

static uint32_t _ts(void) {
    uint32_t uptime = (uint32_t)(to_ms_since_boot(get_absolute_time()) / 1000u);
    return g_epoch_offset ? (uptime + g_epoch_offset) : uptime;
}

void log_boot(BmsLogger *l, float soc, float v) {
    LogEvent ev = {0};
    ev.timestamp_s = _ts();
    ev.type = LOG_BOOT;
    ev.soc_pct = _u8_clamped(soc, 0.0f, 100.0f);
    ev.voltage = v;
    log_write(l, &ev);
}

void log_charge_start(BmsLogger *l, float soc, float v, float current) {
    LogEvent ev = {0};
    ev.timestamp_s = _ts();
    ev.type = LOG_CHARGE_START;
    ev.soc_pct = _u8_clamped(soc, 0.0f, 100.0f);
    ev.voltage = v;
    ev.current = current;
    log_write(l, &ev);
}

void log_discharge_end(BmsLogger *l, float soc, float wh) {
    LogEvent ev = {0};
    ev.timestamp_s = _ts();
    ev.type = LOG_DISCHARGE_END;
    ev.soc_pct = _u8_clamped(soc, 0.0f, 100.0f);
    ev.param = wh;
    log_write(l, &ev);
}

void log_alarm(BmsLogger *l, uint32_t alarms, float soc, float v, float t) {
    LogEvent ev = {0};
    ev.timestamp_s = _ts();
    ev.type = LOG_ALARM;
    ev.soc_pct = _u8_clamped(soc, 0.0f, 100.0f);
    ev.voltage = v;
    ev.temp_bat = _u8_clamped(t, 0.0f, 125.0f);
    ev.alarm_flags = alarms;
    log_write(l, &ev);
}

void log_charge_end(BmsLogger *l, float soc, float wh) {
    LogEvent ev = {0};
    ev.timestamp_s = _ts();
    ev.type = LOG_CHARGE_END;
    ev.soc_pct = _u8_clamped(soc, 0.0f, 100.0f);
    ev.param = wh;
    log_write(l, &ev);
}
