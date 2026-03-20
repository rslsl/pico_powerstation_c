#pragma once
// ============================================================
// bms/flash_nvm.h — Unified safe flash NVM backend
//
// P3.14 fix: single verified implementation used by
//   bms_logger.c, bms_soh.c, bms_stats.c
//
// All flash_range_erase/program calls:
//   - hold multicore_lockout_start_blocking()   (P0.2 / P3.2 fix)
//   - hold save_and_disable_interrupts()
//   - use 4096-byte sector shadow buffer
//
// A/B alternating slot helpers for atomic NVM records (P2.11 fix)
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ── Primitives ───────────────────────────────────────────────
uint32_t nvm_crc32(const void *buf, size_t len);
void     nvm_read (uint32_t flash_offset, void *dst, size_t len);

// Write one full 4096-byte sector (src must be FLASH_SECTOR_SIZE).
// Uses multicore lockout + irq disable internally.
void nvm_write_sector(uint32_t flash_offset, const uint8_t *src);

// Read-modify-write: patch `len` bytes at `offset` inside its sector.
// Reads current sector, applies patch, writes back.
void nvm_patch(uint32_t flash_offset, const void *src, size_t len);

// ── A/B alternating slot ─────────────────────────────────────
// Each slot occupies exactly one sector (4096 bytes).
// Record layout inside a slot (at byte 0):
//   [uint32_t magic] [uint32_t seq] [payload...] [uint32_t crc32]
// CRC covers all bytes before the crc field.
//
// nvm_ab_load() — find the newest valid slot and return its payload.
//   Returns true + fills payload if at least one valid slot found.
//
// nvm_ab_save() — write payload to the OPPOSITE slot to the current,
//   increment seq, so old slot stays valid until new write completes.
//   On success sets *active_slot to new slot index (0 or 1).

#define NVM_SLOT_OVERHEAD  (sizeof(uint32_t)*3)   // magic+seq+crc

bool nvm_ab_load(uint32_t flash_off_a, uint32_t flash_off_b,
                 uint32_t magic,
                 void *payload, size_t payload_sz,
                 uint32_t *seq_out, uint8_t *active_slot_out);

bool nvm_ab_save(uint32_t flash_off_a, uint32_t flash_off_b,
                 uint32_t magic,
                 uint32_t *seq_inout, uint8_t *active_slot_inout,
                 const void *payload, size_t payload_sz);
