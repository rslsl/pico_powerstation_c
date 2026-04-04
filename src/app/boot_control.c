#include "boot_control.h"

#include "../config.h"

#ifndef BOOTCTL_DIRECT_FLASH
#include "../bms/flash_nvm.h"
#else
#include "hardware/address_mapped.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#endif

#include <stdio.h>
#include <string.h>

#define BOOTCTL_MAGIC   0x4C544F42u
#define BOOTCTL_VERSION 1u

static uint32_t g_bootctl_seq = 0u;
static uint8_t g_bootctl_slot = 0u;

static const BootSlotRegion k_slot_regions[] = {
    { BOOT_SLOT_A, PICO_OTA_SLOT_A_OFFSET, PICO_OTA_SLOT_SIZE, "slot-a" },
    { BOOT_SLOT_B, PICO_OTA_SLOT_B_OFFSET, PICO_OTA_SLOT_SIZE, "slot-b" },
};

#ifdef BOOTCTL_DIRECT_FLASH
static bool _seq_is_newer(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0;
}
static void _bootctl_read(uint32_t flash_offset, void *dst, size_t len) {
    memcpy(dst, (const void *)(uintptr_t)(XIP_BASE + flash_offset), len);
}

static uint32_t _bootctl_crc32(const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static bool _bootctl_slot_valid(uint32_t flash_off,
                                uint32_t magic,
                                size_t payload_sz,
                                uint32_t *seq_out) {
    size_t rec_sz = 8u + payload_sz + 4u;
    uint32_t stored_magic;
    uint32_t stored_seq;
    uint32_t stored_crc;
    uint32_t calc_crc;
    static uint8_t buf[FLASH_SECTOR_SIZE];

    if (!seq_out || rec_sz > FLASH_SECTOR_SIZE) return false;
    _bootctl_read(flash_off, buf, rec_sz);

    memcpy(&stored_magic, buf + 0u, 4u);
    memcpy(&stored_seq, buf + 4u, 4u);
    memcpy(&stored_crc, buf + 8u + payload_sz, 4u);
    if (stored_magic != magic) return false;

    calc_crc = _bootctl_crc32(buf, 8u + payload_sz);
    if (calc_crc != stored_crc) return false;

    *seq_out = stored_seq;
    return true;
}

static bool _bootctl_ab_load(uint32_t flash_off_a, uint32_t flash_off_b,
                             uint32_t magic,
                             void *payload, size_t payload_sz,
                             uint32_t *seq_out, uint8_t *active_slot_out) {
    uint32_t seq_a = 0u;
    uint32_t seq_b = 0u;
    bool valid_a = _bootctl_slot_valid(flash_off_a, magic, payload_sz, &seq_a);
    bool valid_b = _bootctl_slot_valid(flash_off_b, magic, payload_sz, &seq_b);
    uint32_t best_off;
    uint32_t best_seq;
    uint8_t best_slot;

    if (!valid_a && !valid_b) return false;

    if (valid_a && valid_b) {
        best_slot = _seq_is_newer(seq_a, seq_b) || seq_a == seq_b ? 0u : 1u;
        best_off = best_slot == 0u ? flash_off_a : flash_off_b;
        best_seq = best_slot == 0u ? seq_a : seq_b;
    } else if (valid_a) {
        best_slot = 0u;
        best_off = flash_off_a;
        best_seq = seq_a;
    } else {
        best_slot = 1u;
        best_off = flash_off_b;
        best_seq = seq_b;
    }

    _bootctl_read(best_off + 8u, payload, payload_sz);
    if (seq_out) *seq_out = best_seq;
    if (active_slot_out) *active_slot_out = best_slot;
    return true;
}

static void _bootctl_write_sector(uint32_t flash_offset, const uint8_t *src) {
    uint32_t ints;

    if (!src) return;
    ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
    for (size_t off = 0; off < (size_t)FLASH_SECTOR_SIZE; off += FLASH_PAGE_SIZE) {
        flash_range_program(flash_offset + (uint32_t)off, src + off, FLASH_PAGE_SIZE);
    }
    restore_interrupts(ints);
}

static bool _bootctl_ab_save(uint32_t flash_off_a, uint32_t flash_off_b,
                             uint32_t magic,
                             uint32_t *seq_inout, uint8_t *active_slot_inout,
                             const void *payload, size_t payload_sz) {
    size_t rec_sz = 8u + payload_sz + 4u;
    uint8_t next_slot;
    uint32_t next_off;
    uint32_t crc;
    static uint8_t sector_buf[FLASH_SECTOR_SIZE];

    if (!seq_inout || !active_slot_inout || !payload || rec_sz > FLASH_SECTOR_SIZE) return false;

    next_slot = (*active_slot_inout == 0u) ? 1u : 0u;
    next_off = next_slot == 0u ? flash_off_a : flash_off_b;
    (*seq_inout)++;

    memset(sector_buf, 0xFF, sizeof(sector_buf));
    memcpy(sector_buf + 0u, &magic, 4u);
    memcpy(sector_buf + 4u, seq_inout, 4u);
    memcpy(sector_buf + 8u, payload, payload_sz);
    crc = _bootctl_crc32(sector_buf, 8u + payload_sz);
    memcpy(sector_buf + 8u + payload_sz, &crc, 4u);

    _bootctl_write_sector(next_off, sector_buf);
    *active_slot_inout = next_slot;
    return true;
}
#else
#define _bootctl_ab_load nvm_ab_load
#define _bootctl_ab_save nvm_ab_save
#endif

static bool _slot_is_known(BootSlot slot) {
    return slot == BOOT_SLOT_A || slot == BOOT_SLOT_B;
}

static int _slot_index(BootSlot slot) {
    if (slot == BOOT_SLOT_A) return 0;
    if (slot == BOOT_SLOT_B) return 1;
    return -1;
}

static void _image_reset(BootImageInfo *image) {
    if (!image) return;
    memset(image, 0, sizeof(*image));
}

static bool _image_is_bootable(const BootImageInfo *image) {
    if (!image) return false;
    if ((image->flags & (BOOT_IMAGE_FLAG_PRESENT | BOOT_IMAGE_FLAG_VERIFIED)) !=
        (BOOT_IMAGE_FLAG_PRESENT | BOOT_IMAGE_FLAG_VERIFIED)) {
        return false;
    }
    return image->image_size > 0u && image->image_size <= PICO_OTA_SLOT_SIZE;
}

static void _sanitize(BootControlState *state) {
    if (!state) return;

    if (state->version != BOOTCTL_VERSION) {
        bootctl_defaults(state);
        return;
    }

    if (state->loader_size != PICO_OTA_LOADER_SIZE) state->loader_size = PICO_OTA_LOADER_SIZE;
    if (state->slot_size != PICO_OTA_SLOT_SIZE) state->slot_size = PICO_OTA_SLOT_SIZE;
    if (state->max_boot_attempts == 0u || state->max_boot_attempts > 10u) {
        state->max_boot_attempts = PICO_OTA_MAX_BOOT_ATTEMPTS;
    }

    for (int i = 0; i < 2; ++i) {
        BootImageInfo *image = &state->images[i];
        if (!_image_is_bootable(image)) {
            bool keep_present = (image->flags & BOOT_IMAGE_FLAG_PRESENT) != 0u &&
                                image->image_size <= PICO_OTA_SLOT_SIZE;
            uint32_t old_size = keep_present ? image->image_size : 0u;
            char old_version[sizeof(image->version)];
            memcpy(old_version, image->version, sizeof(old_version));
            _image_reset(image);
            if (keep_present && old_size > 0u) {
                image->image_size = old_size;
                image->flags = BOOT_IMAGE_FLAG_PRESENT;
                memcpy(image->version, old_version, sizeof(image->version));
                image->version[sizeof(image->version) - 1u] = '\0';
            }
        }
        if (image->boot_attempts > state->max_boot_attempts) {
            image->boot_attempts = state->max_boot_attempts;
        }
    }

    if (!_slot_is_known((BootSlot)state->active_slot) ||
        !_image_is_bootable(&state->images[_slot_index((BootSlot)state->active_slot)])) {
        state->active_slot = BOOT_SLOT_NONE;
    }
    if (!_slot_is_known((BootSlot)state->confirmed_slot) ||
        !_image_is_bootable(&state->images[_slot_index((BootSlot)state->confirmed_slot)])) {
        state->confirmed_slot = BOOT_SLOT_NONE;
    }
    if (!_slot_is_known((BootSlot)state->pending_slot) ||
        !_image_is_bootable(&state->images[_slot_index((BootSlot)state->pending_slot)])) {
        state->pending_slot = BOOT_SLOT_NONE;
    }
    if (!_slot_is_known((BootSlot)state->last_boot_slot)) {
        state->last_boot_slot = BOOT_SLOT_NONE;
    }

    for (int i = 0; i < 2; ++i) {
        state->images[i].flags &= ~(uint32_t)BOOT_IMAGE_FLAG_CONFIRMED;
    }
    if (_slot_is_known((BootSlot)state->confirmed_slot)) {
        state->images[_slot_index((BootSlot)state->confirmed_slot)].flags |= BOOT_IMAGE_FLAG_CONFIRMED;
    }

    if (state->active_slot == BOOT_SLOT_NONE && state->confirmed_slot != BOOT_SLOT_NONE) {
        state->active_slot = state->confirmed_slot;
    }
}

void bootctl_defaults(BootControlState *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->version = BOOTCTL_VERSION;
    out->max_boot_attempts = PICO_OTA_MAX_BOOT_ATTEMPTS;
    out->loader_size = PICO_OTA_LOADER_SIZE;
    out->slot_size = PICO_OTA_SLOT_SIZE;
}

bool bootctl_load(BootControlState *out) {
    BootControlState tmp;

    if (!out) return false;
    if (!_bootctl_ab_load(PICO_OTA_CTRL_OFFSET_A, PICO_OTA_CTRL_OFFSET_B,
                          BOOTCTL_MAGIC, &tmp, sizeof(tmp),
                          &g_bootctl_seq, &g_bootctl_slot)) {
        bootctl_defaults(out);
        return false;
    }

    _sanitize(&tmp);
    *out = tmp;
    return true;
}

bool bootctl_store(const BootControlState *state) {
    BootControlState tmp;

    if (!state) return false;
    tmp = *state;
    _sanitize(&tmp);

    if (!_bootctl_ab_save(PICO_OTA_CTRL_OFFSET_A, PICO_OTA_CTRL_OFFSET_B,
                          BOOTCTL_MAGIC, &g_bootctl_seq, &g_bootctl_slot,
                          &tmp, sizeof(tmp))) {
#ifndef BOOTCTL_DIRECT_FLASH
        printf("[BOOTCTL] store FAILED\n");
#endif
        return false;
    }

#ifndef BOOTCTL_DIRECT_FLASH
    printf("[BOOTCTL] saved active=%u pending=%u confirmed=%u\n",
           (unsigned)tmp.active_slot,
           (unsigned)tmp.pending_slot,
           (unsigned)tmp.confirmed_slot);
#endif
    return true;
}

const BootSlotRegion *bootctl_slot_region(BootSlot slot) {
    int idx = _slot_index(slot);
    if (idx < 0) return NULL;
    return &k_slot_regions[idx];
}

BootSlot bootctl_active_slot(const BootControlState *state) {
    if (!state) return BOOT_SLOT_NONE;
    if (_slot_is_known((BootSlot)state->active_slot)) {
        return (BootSlot)state->active_slot;
    }
    if (_slot_is_known((BootSlot)state->confirmed_slot)) {
        return (BootSlot)state->confirmed_slot;
    }
    return BOOT_SLOT_NONE;
}

BootSlot bootctl_inactive_slot(const BootControlState *state) {
    BootSlot active = bootctl_active_slot(state);
    if (active == BOOT_SLOT_A) return BOOT_SLOT_B;
    return BOOT_SLOT_A;
}

bool bootctl_slot_can_fit(BootSlot slot, uint32_t image_size) {
    const BootSlotRegion *region = bootctl_slot_region(slot);
    return region && image_size > 0u && image_size <= region->max_size;
}

bool bootctl_slot_has_bootable_image(const BootControlState *state, BootSlot slot) {
    int idx;
    if (!state) return false;
    idx = _slot_index(slot);
    if (idx < 0) return false;
    return _image_is_bootable(&state->images[idx]);
}

BootSlot bootctl_select_boot_slot(const BootControlState *state) {
    BootSlot pending;
    BootSlot active;

    if (!state) return BOOT_SLOT_NONE;

    pending = (BootSlot)state->pending_slot;
    if (_slot_is_known(pending)) {
        const BootImageInfo *image = &state->images[_slot_index(pending)];
        if (_image_is_bootable(image) && image->boot_attempts < state->max_boot_attempts) {
            return pending;
        }
    }

    active = bootctl_active_slot(state);
    if (_slot_is_known(active) && bootctl_slot_has_bootable_image(state, active)) {
        return active;
    }
    if (bootctl_slot_has_bootable_image(state, BOOT_SLOT_A)) return BOOT_SLOT_A;
    if (bootctl_slot_has_bootable_image(state, BOOT_SLOT_B)) return BOOT_SLOT_B;
    return BOOT_SLOT_NONE;
}

bool bootctl_stage_update(BootControlState *state,
                          BootSlot slot,
                          uint32_t image_size,
                          uint32_t image_crc32,
                          const char *version_tag) {
    int idx;
    BootImageInfo *image;

    if (!state || !_slot_is_known(slot) || !bootctl_slot_can_fit(slot, image_size)) return false;
    _sanitize(state);

    idx = _slot_index(slot);
    image = &state->images[idx];
    _image_reset(image);
    image->image_size = image_size;
    image->image_crc32 = image_crc32;
    image->flags = BOOT_IMAGE_FLAG_PRESENT | BOOT_IMAGE_FLAG_VERIFIED;
    if (version_tag && version_tag[0]) {
        snprintf(image->version, sizeof(image->version), "%s", version_tag);
    }

    state->pending_slot = (uint8_t)slot;
    state->rollback_reason = BOOT_ROLLBACK_NONE;
    return true;
}

bool bootctl_mark_boot_attempt(BootControlState *state, BootSlot slot) {
    int idx;
    if (!state || !_slot_is_known(slot)) return false;
    idx = _slot_index(slot);
    if (!_image_is_bootable(&state->images[idx])) return false;

    if (state->images[idx].boot_attempts < UINT32_MAX) {
        state->images[idx].boot_attempts++;
    }
    state->last_boot_slot = (uint8_t)slot;
    return true;
}

bool bootctl_mark_confirmed(BootControlState *state, BootSlot slot) {
    int idx;
    if (!state || !_slot_is_known(slot)) return false;
    idx = _slot_index(slot);
    if (!_image_is_bootable(&state->images[idx])) return false;

    for (int i = 0; i < 2; ++i) {
        state->images[i].flags &= ~(uint32_t)BOOT_IMAGE_FLAG_CONFIRMED;
    }
    state->images[idx].flags |= BOOT_IMAGE_FLAG_CONFIRMED;
    state->images[idx].boot_attempts = 0u;
    state->active_slot = (uint8_t)slot;
    state->confirmed_slot = (uint8_t)slot;
    state->pending_slot = BOOT_SLOT_NONE;
    state->last_boot_slot = (uint8_t)slot;
    state->rollback_reason = BOOT_ROLLBACK_NONE;
    return true;
}

bool bootctl_mark_rollback(BootControlState *state,
                           BootSlot fallback_slot,
                           BootRollbackReason reason) {
    if (!state) return false;
    _sanitize(state);

    state->pending_slot = BOOT_SLOT_NONE;
    state->rollback_reason = (uint8_t)reason;
    if (_slot_is_known(fallback_slot) && bootctl_slot_has_bootable_image(state, fallback_slot)) {
        state->active_slot = (uint8_t)fallback_slot;
        if (state->confirmed_slot == BOOT_SLOT_NONE) {
            state->confirmed_slot = (uint8_t)fallback_slot;
        }
    } else {
        state->active_slot = BOOT_SLOT_NONE;
        if (!_slot_is_known((BootSlot)state->confirmed_slot) ||
            !bootctl_slot_has_bootable_image(state, (BootSlot)state->confirmed_slot)) {
            state->confirmed_slot = BOOT_SLOT_NONE;
        }
    }
    return true;
}

bool bootctl_clear_slot(BootControlState *state, BootSlot slot) {
    int idx;
    if (!state || !_slot_is_known(slot)) return false;
    idx = _slot_index(slot);
    _image_reset(&state->images[idx]);
    if (state->active_slot == (uint8_t)slot) state->active_slot = BOOT_SLOT_NONE;
    if (state->pending_slot == (uint8_t)slot) state->pending_slot = BOOT_SLOT_NONE;
    if (state->confirmed_slot == (uint8_t)slot) state->confirmed_slot = BOOT_SLOT_NONE;
    if (state->last_boot_slot == (uint8_t)slot) state->last_boot_slot = BOOT_SLOT_NONE;
    return true;
}
