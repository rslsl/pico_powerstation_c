// ============================================================
// bms/flash_nvm.c — Unified safe flash NVM backend
// ============================================================
#include "flash_nvm.h"
#include "../config.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static inline void _nvm_single_core_guard(void) {
    // Current firmware writes NVM only from Core0; keep this explicit
    // because several helpers use shared static buffers.
    assert(get_core_num() == 0);
}

static bool _seq_is_newer(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0;
}

// ── CRC32 ────────────────────────────────────────────────────
uint32_t nvm_crc32(const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

// ── Raw read (XIP) ───────────────────────────────────────────
void nvm_read(uint32_t flash_offset, void *dst, size_t len) {
    memcpy(dst, (const uint8_t *)(0x10000000u + flash_offset), len);
}

// ── Safe sector write ─────────────────────────────────────────
// P0.2 / P3.14 fix: multicore_lockout + irq disable for ALL flash ops.
// Core1 must call multicore_lockout_victim_init() at startup.
void nvm_write_sector(uint32_t flash_offset, const uint8_t *src) {
    _nvm_single_core_guard();
    assert((flash_offset % FLASH_SECTOR_SIZE) == 0u);
    // flash_offset must be sector-aligned
    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
    for (size_t off = 0; off < (size_t)FLASH_SECTOR_SIZE; off += 256)
        flash_range_program(flash_offset + off, src + off, 256);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
}

void nvm_erase_sector(uint32_t flash_offset) {
    _nvm_single_core_guard();
    assert((flash_offset % FLASH_SECTOR_SIZE) == 0u);
    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
}

void nvm_program_page(uint32_t flash_offset, const uint8_t *src) {
    _nvm_single_core_guard();
    assert((flash_offset % FLASH_PAGE_SIZE) == 0u);
    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(flash_offset, src, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
}

// ── Patch (read-modify-write) ────────────────────────────────
void nvm_patch(uint32_t flash_offset, const void *src, size_t len) {
    _nvm_single_core_guard();
    uint32_t sector_start = (flash_offset / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    uint32_t in_sector    = flash_offset - sector_start;
    assert((in_sector + len) <= FLASH_SECTOR_SIZE);

    static uint8_t sector_buf[FLASH_SECTOR_SIZE];
    nvm_read(sector_start, sector_buf, FLASH_SECTOR_SIZE);

    size_t copy_len = len;
    if (in_sector + copy_len > FLASH_SECTOR_SIZE)
        copy_len = FLASH_SECTOR_SIZE - in_sector;
    memcpy(sector_buf + in_sector, src, copy_len);

    nvm_write_sector(sector_start, sector_buf);
}

// ── A/B slot helpers ─────────────────────────────────────────
// Slot layout (within a 4096-byte sector):
//   [0..3]  uint32_t magic
//   [4..7]  uint32_t seq
//   [8..8+payload_sz-1] payload
//   [8+payload_sz..11+payload_sz] uint32_t crc32  (covers bytes 0..8+payload_sz-1)

static bool _slot_valid(uint32_t flash_off, uint32_t magic,
                        size_t payload_sz, uint32_t *seq_out) {
    _nvm_single_core_guard();
    // We only need first (8 + payload_sz + 4) bytes
    size_t rec_sz = 8 + payload_sz + 4;
    if (rec_sz > FLASH_SECTOR_SIZE) return false;

    static uint8_t buf[FLASH_SECTOR_SIZE];
    nvm_read(flash_off, buf, rec_sz);

    uint32_t m, s, c_stored;
    memcpy(&m, buf + 0, 4);
    memcpy(&s, buf + 4, 4);
    memcpy(&c_stored, buf + 8 + payload_sz, 4);
    if (m != magic) return false;

    uint32_t c_calc = nvm_crc32(buf, 8 + payload_sz);
    if (c_calc != c_stored) return false;

    *seq_out = s;
    return true;
}

bool nvm_ab_load(uint32_t flash_off_a, uint32_t flash_off_b,
                 uint32_t magic,
                 void *payload, size_t payload_sz,
                 uint32_t *seq_out, uint8_t *active_slot_out) {
    _nvm_single_core_guard();
    uint32_t seq_a = 0, seq_b = 0;
    bool va = _slot_valid(flash_off_a, magic, payload_sz, &seq_a);
    bool vb = _slot_valid(flash_off_b, magic, payload_sz, &seq_b);

    if (!va && !vb) return false;

    uint32_t best_off;
    uint32_t best_seq;
    uint8_t  slot;

    if      (va && vb) { slot = (_seq_is_newer(seq_a, seq_b) || seq_a == seq_b) ? 0 : 1;
                         best_off = (slot == 0) ? flash_off_a : flash_off_b;
                         best_seq = (slot == 0) ? seq_a : seq_b; }
    else if (va)       { slot = 0; best_off = flash_off_a; best_seq = seq_a; }
    else               { slot = 1; best_off = flash_off_b; best_seq = seq_b; }

    // Read payload from best slot
    nvm_read(best_off + 8, payload, payload_sz);
    if (seq_out)         *seq_out         = best_seq;
    if (active_slot_out) *active_slot_out = slot;
    return true;
}

bool nvm_ab_save(uint32_t flash_off_a, uint32_t flash_off_b,
                 uint32_t magic,
                 uint32_t *seq_inout, uint8_t *active_slot_inout,
                 const void *payload, size_t payload_sz) {
    _nvm_single_core_guard();
    size_t rec_sz = 8 + payload_sz + 4;
    if (rec_sz > FLASH_SECTOR_SIZE) return false;

    // Write to OPPOSITE slot for atomicity:
    // if power lost mid-write, old slot is still valid.
    uint8_t next_slot = (*active_slot_inout == 0) ? 1 : 0;
    uint32_t next_off = (next_slot == 0) ? flash_off_a : flash_off_b;
    (*seq_inout)++;

    static uint8_t sector_buf[FLASH_SECTOR_SIZE];
    memset(sector_buf, 0xFF, FLASH_SECTOR_SIZE);

    memcpy(sector_buf + 0, &magic,      4);
    memcpy(sector_buf + 4, seq_inout,   4);
    memcpy(sector_buf + 8, payload,     payload_sz);
    uint32_t crc = nvm_crc32(sector_buf, 8 + payload_sz);
    memcpy(sector_buf + 8 + payload_sz, &crc, 4);

    nvm_write_sector(next_off, sector_buf);
    *active_slot_inout = next_slot;
    return true;
}
