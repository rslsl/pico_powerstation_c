#include "ota_manager.h"

#include "../config.h"

#include "hardware/address_mapped.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

#include <stdio.h>
#include <string.h>

#ifndef PICO_OTA_CURRENT_SLOT
#define PICO_OTA_CURRENT_SLOT 0
#endif

#define OTA_VECTOR_OFFSET 0x100u
#define OTA_RAM_START     0x20000000u
#define OTA_RAM_END       0x20042000u

static uint32_t _crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    if (!data && len != 0u) return crc;
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void _set_error(PicoOtaManager *ota, const char *reason) {
    if (!ota) return;
    ota->state = (uint8_t)PICO_OTA_STATE_ERROR;
    ota->session_active = false;
    ota->reboot_pending = false;
    snprintf(ota->last_error, sizeof(ota->last_error), "%s", reason ? reason : "error");
}

static void _set_detail(char *detail, size_t detail_sz, const char *text) {
    if (!detail || detail_sz == 0u) return;
    snprintf(detail, detail_sz, "%s", text ? text : "");
}

static void _flash_erase_region(uint32_t flash_offset, uint32_t length) {
    uint32_t ints;
    multicore_lockout_start_blocking();
    ints = save_and_disable_interrupts();
    for (uint32_t off = 0u; off < length; off += FLASH_SECTOR_SIZE) {
        flash_range_erase(flash_offset + off, FLASH_SECTOR_SIZE);
    }
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
}

static void _flash_program_page(uint32_t flash_offset, const uint8_t *page_data) {
    uint32_t ints;
    multicore_lockout_start_blocking();
    ints = save_and_disable_interrupts();
    flash_range_program(flash_offset, page_data, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
}

static bool _slot_vectors_valid(BootSlot slot) {
    const BootSlotRegion *region = bootctl_slot_region(slot);
    uint32_t vector_addr;
    uint32_t sp;
    uint32_t pc;
    uint32_t pc_min;
    uint32_t pc_max;

    if (!region) return false;

    vector_addr = XIP_BASE + region->flash_offset + OTA_VECTOR_OFFSET;
    sp = *(const uint32_t *)(uintptr_t)vector_addr;
    pc = *(const uint32_t *)(uintptr_t)(vector_addr + 4u);
    pc_min = XIP_BASE + region->flash_offset + OTA_VECTOR_OFFSET;
    pc_max = XIP_BASE + region->flash_offset + region->max_size;

    if (sp < OTA_RAM_START || sp > OTA_RAM_END || ((sp & 0x3u) != 0u)) return false;
    if ((pc & 0x1u) == 0u || pc < pc_min || pc >= pc_max) return false;
    return true;
}

static uint32_t _flash_crc32(uint32_t flash_offset, uint32_t len) {
    const uint8_t *data = (const uint8_t *)(uintptr_t)(XIP_BASE + flash_offset);
    return _crc32_update(0u, data, len);
}

static bool _flush_partial_page(PicoOtaManager *ota) {
    const BootSlotRegion *region;
    uint32_t page_abs_offset;

    if (!ota || ota->page_fill == 0u) return true;
    region = bootctl_slot_region(ota->target_slot);
    if (!region) return false;

    page_abs_offset = region->flash_offset + ota->page_base_offset;
    _flash_program_page(page_abs_offset, ota->page_buf);
    ota->page_fill = 0u;
    return true;
}

BootSlot pico_ota_running_slot(void) {
#if PICO_OTA_CURRENT_SLOT == 1
    return BOOT_SLOT_A;
#elif PICO_OTA_CURRENT_SLOT == 2
    return BOOT_SLOT_B;
#else
    return BOOT_SLOT_NONE;
#endif
}

BootSlot pico_ota_target_slot(void) {
    BootSlot running = pico_ota_running_slot();
    if (running == BOOT_SLOT_A) return BOOT_SLOT_B;
    if (running == BOOT_SLOT_B) return BOOT_SLOT_A;
    return BOOT_SLOT_NONE;
}

const char *pico_ota_slot_name(BootSlot slot) {
    switch (slot) {
        case BOOT_SLOT_A: return "slot-a";
        case BOOT_SLOT_B: return "slot-b";
        case BOOT_SLOT_NONE:
        default:          return "none";
    }
}

const char *pico_ota_state_name(PicoOtaState state) {
    switch (state) {
        case PICO_OTA_STATE_ERASING:    return "erasing";
        case PICO_OTA_STATE_RECEIVING:  return "receiving";
        case PICO_OTA_STATE_FINALIZING: return "finalizing";
        case PICO_OTA_STATE_READY:      return "ready";
        case PICO_OTA_STATE_ERROR:      return "error";
        case PICO_OTA_STATE_IDLE:
        default:                        return "idle";
    }
}

void pico_ota_init(PicoOtaManager *ota) {
    if (!ota) return;
    memset(ota, 0, sizeof(*ota));
    ota->state = (uint8_t)PICO_OTA_STATE_IDLE;
    ota->target_slot = BOOT_SLOT_NONE;
}

bool pico_ota_is_supported(void) {
    return pico_ota_running_slot() != BOOT_SLOT_NONE &&
           pico_ota_target_slot() != BOOT_SLOT_NONE;
}

bool pico_ota_is_busy(const PicoOtaManager *ota) {
    if (!ota) return false;
    return ota->session_active ||
           ota->state == (uint8_t)PICO_OTA_STATE_ERASING ||
           ota->state == (uint8_t)PICO_OTA_STATE_RECEIVING ||
           ota->state == (uint8_t)PICO_OTA_STATE_FINALIZING;
}

bool pico_ota_begin(PicoOtaManager *ota,
                    uint32_t image_size,
                    const char *version_tag,
                    char *detail,
                    size_t detail_sz) {
    const BootSlotRegion *region;
    uint32_t erase_len;
    BootSlot target;

    if (!ota) return false;
    pico_ota_abort(ota, NULL);

    target = pico_ota_target_slot();
    region = bootctl_slot_region(target);
    if (!pico_ota_is_supported() || !region) {
        _set_error(ota, "slot-layout-unsupported");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }
    if (image_size == 0u || image_size > region->max_size) {
        _set_error(ota, "image-size-invalid");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }

    ota->state = (uint8_t)PICO_OTA_STATE_ERASING;
    ota->target_slot = target;
    ota->image_size = image_size;
    ota->bytes_written = 0u;
    ota->expected_crc32 = 0u;
    ota->reboot_pending = false;
    ota->page_base_offset = 0u;
    ota->page_fill = 0u;
    memset(ota->page_buf, 0xFF, sizeof(ota->page_buf));
    ota->last_error[0] = '\0';
    snprintf(ota->version, sizeof(ota->version), "%s",
             (version_tag && version_tag[0]) ? version_tag : "web-ota");

    erase_len = (image_size + FLASH_SECTOR_SIZE - 1u) & ~(FLASH_SECTOR_SIZE - 1u);
    _flash_erase_region(region->flash_offset, erase_len);

    ota->state = (uint8_t)PICO_OTA_STATE_RECEIVING;
    ota->session_active = true;
    _set_detail(detail, detail_sz, pico_ota_slot_name(target));
    return true;
}

bool pico_ota_write_chunk(PicoOtaManager *ota,
                          uint32_t offset,
                          const uint8_t *data,
                          size_t len,
                          char *detail,
                          size_t detail_sz) {
    const BootSlotRegion *region;

    if (!ota || !data || len == 0u) return false;
    if (!ota->session_active || ota->state != (uint8_t)PICO_OTA_STATE_RECEIVING) {
        _set_error(ota, "session-not-active");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }
    if (len > PICO_OTA_UART_CHUNK_MAX) {
        _set_error(ota, "chunk-too-large");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }
    if (offset != ota->bytes_written) {
        _set_error(ota, "offset-mismatch");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }
    if ((uint64_t)offset + (uint64_t)len > (uint64_t)ota->image_size) {
        _set_error(ota, "chunk-overflow");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }

    region = bootctl_slot_region(ota->target_slot);
    if (!region) {
        _set_error(ota, "target-slot-missing");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }

    while (len > 0u) {
        uint32_t page_offset = ota->bytes_written % FLASH_PAGE_SIZE;
        size_t room = FLASH_PAGE_SIZE - page_offset;
        size_t copy_len = (len < room) ? len : room;

        if (page_offset == 0u) {
            ota->page_base_offset = ota->bytes_written;
            memset(ota->page_buf, 0xFF, sizeof(ota->page_buf));
            ota->page_fill = 0u;
        }

        memcpy(ota->page_buf + page_offset, data, copy_len);
        ota->bytes_written += (uint32_t)copy_len;
        if ((uint16_t)(page_offset + copy_len) > ota->page_fill) {
            ota->page_fill = (uint16_t)(page_offset + copy_len);
        }
        data += copy_len;
        len -= copy_len;

        if ((ota->bytes_written % FLASH_PAGE_SIZE) == 0u) {
            _flash_program_page(region->flash_offset + ota->page_base_offset, ota->page_buf);
            ota->page_fill = 0u;
        }
    }

    snprintf(detail, detail_sz, "%lu/%lu",
             (unsigned long)ota->bytes_written,
             (unsigned long)ota->image_size);
    return true;
}

bool pico_ota_finalize(PicoOtaManager *ota,
                       uint32_t expected_crc32,
                       uint32_t now_ms,
                       char *detail,
                       size_t detail_sz) {
    BootControlState state;
    bool have_state;
    uint32_t actual_crc32;
    const BootSlotRegion *region;

    if (!ota) return false;
    if (!ota->session_active || ota->state != (uint8_t)PICO_OTA_STATE_RECEIVING) {
        _set_error(ota, "session-not-active");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }
    if (ota->bytes_written != ota->image_size) {
        _set_error(ota, "size-mismatch");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }

    ota->state = (uint8_t)PICO_OTA_STATE_FINALIZING;
    region = bootctl_slot_region(ota->target_slot);
    if (!region || !_flush_partial_page(ota)) {
        _set_error(ota, "flash-flush-failed");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }

    if (!_slot_vectors_valid(ota->target_slot)) {
        _set_error(ota, "bad-vector-table");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }

    actual_crc32 = _flash_crc32(region->flash_offset, ota->image_size);
    ota->expected_crc32 = expected_crc32;
    if (actual_crc32 != expected_crc32) {
        _set_error(ota, "crc-mismatch");
        snprintf(ota->last_error, sizeof(ota->last_error),
                 "crc-mismatch:%08lX!=%08lX",
                 (unsigned long)actual_crc32,
                 (unsigned long)expected_crc32);
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }

    have_state = bootctl_load(&state);
    if (!have_state) bootctl_defaults(&state);

    if (!bootctl_stage_update(&state, ota->target_slot, ota->image_size, actual_crc32, ota->version)) {
        _set_error(ota, "bootctl-stage-failed");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }
    if (!bootctl_store(&state)) {
        _set_error(ota, "bootctl-store-failed");
        _set_detail(detail, detail_sz, ota->last_error);
        return false;
    }

    ota->state = (uint8_t)PICO_OTA_STATE_READY;
    ota->session_active = false;
    ota->reboot_pending = true;
    ota->reboot_at_ms = now_ms + PICO_OTA_REBOOT_DELAY_MS;
    ota->last_error[0] = '\0';

    snprintf(detail, detail_sz, "%s:%08lX",
             pico_ota_slot_name(ota->target_slot),
             (unsigned long)actual_crc32);
    return true;
}

void pico_ota_abort(PicoOtaManager *ota, const char *reason) {
    if (!ota) return;
    ota->session_active = false;
    ota->reboot_pending = false;
    ota->page_fill = 0u;
    if (reason && reason[0]) {
        _set_error(ota, reason);
    } else if (ota->state == (uint8_t)PICO_OTA_STATE_ERROR) {
        /* preserve last error */
    } else {
        ota->state = (uint8_t)PICO_OTA_STATE_IDLE;
        ota->last_error[0] = '\0';
    }
}

bool pico_ota_should_reboot(const PicoOtaManager *ota, uint32_t now_ms) {
    return ota && ota->reboot_pending && now_ms >= ota->reboot_at_ms;
}

void pico_ota_fill_status(const PicoOtaManager *ota, PicoOtaStatus *out) {
    BootControlState state;
    bool have_state;

    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->running_slot = pico_ota_running_slot();
    out->target_slot = pico_ota_target_slot();
    out->state = PICO_OTA_STATE_IDLE;

    if (ota) {
        out->state = (PicoOtaState)ota->state;
        out->target_slot = ota->target_slot != BOOT_SLOT_NONE ? ota->target_slot : out->target_slot;
        out->image_size = ota->image_size;
        out->bytes_written = ota->bytes_written;
        out->expected_crc32 = ota->expected_crc32;
        out->reboot_pending = ota->reboot_pending;
        snprintf(out->version, sizeof(out->version), "%s", ota->version);
        snprintf(out->last_error, sizeof(out->last_error), "%s", ota->last_error);
    }

    have_state = bootctl_load(&state);
    if (have_state) {
        out->active_slot = (BootSlot)state.active_slot;
        out->pending_slot = (BootSlot)state.pending_slot;
        out->confirmed_slot = (BootSlot)state.confirmed_slot;
    }
}
