#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_control.h"

typedef enum {
    PICO_OTA_STATE_IDLE = 0,
    PICO_OTA_STATE_ERASING,
    PICO_OTA_STATE_RECEIVING,
    PICO_OTA_STATE_FINALIZING,
    PICO_OTA_STATE_READY,
    PICO_OTA_STATE_ERROR,
} PicoOtaState;

typedef struct {
    PicoOtaState state;
    BootSlot running_slot;
    BootSlot target_slot;
    BootSlot active_slot;
    BootSlot pending_slot;
    BootSlot confirmed_slot;
    uint32_t image_size;
    uint32_t bytes_written;
    uint32_t expected_crc32;
    bool reboot_pending;
    char version[24];
    char last_error[48];
} PicoOtaStatus;

typedef struct {
    uint8_t state;
    bool session_active;
    bool reboot_pending;
    BootSlot target_slot;
    uint32_t image_size;
    uint32_t bytes_written;
    uint32_t expected_crc32;
    uint32_t reboot_at_ms;
    char version[24];
    char last_error[48];
    uint32_t page_base_offset;
    uint16_t page_fill;
    uint8_t page_buf[256];
} PicoOtaManager;

BootSlot pico_ota_running_slot(void);
BootSlot pico_ota_target_slot(void);
const char *pico_ota_slot_name(BootSlot slot);
const char *pico_ota_state_name(PicoOtaState state);

void pico_ota_init(PicoOtaManager *ota);
bool pico_ota_is_supported(void);
bool pico_ota_is_busy(const PicoOtaManager *ota);

bool pico_ota_begin(PicoOtaManager *ota,
                    uint32_t image_size,
                    const char *version_tag,
                    char *detail,
                    size_t detail_sz);
bool pico_ota_write_chunk(PicoOtaManager *ota,
                          uint32_t offset,
                          const uint8_t *data,
                          size_t len,
                          char *detail,
                          size_t detail_sz);
bool pico_ota_finalize(PicoOtaManager *ota,
                       uint32_t expected_crc32,
                       uint32_t now_ms,
                       char *detail,
                       size_t detail_sz);
void pico_ota_abort(PicoOtaManager *ota, const char *reason);
bool pico_ota_should_reboot(const PicoOtaManager *ota, uint32_t now_ms);
void pico_ota_fill_status(const PicoOtaManager *ota, PicoOtaStatus *out);
