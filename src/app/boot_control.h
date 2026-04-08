#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOOT_SLOT_NONE = 0,
    BOOT_SLOT_A = 1,
    BOOT_SLOT_B = 2,
} BootSlot;

typedef enum {
    BOOT_ROLLBACK_NONE = 0,
    BOOT_ROLLBACK_BAD_IMAGE = 1,
    BOOT_ROLLBACK_BOOT_LIMIT = 2,
    BOOT_ROLLBACK_MANUAL = 3,
} BootRollbackReason;

typedef enum {
    BOOT_IMAGE_FLAG_PRESENT = 1u << 0,
    BOOT_IMAGE_FLAG_VERIFIED = 1u << 1,
    BOOT_IMAGE_FLAG_CONFIRMED = 1u << 2,
} BootImageFlags;

typedef struct {
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t flags;
    uint32_t boot_attempts;
    char version[24];
} BootImageInfo;

typedef struct {
    uint16_t version;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t confirmed_slot;
    uint8_t last_boot_slot;
    uint8_t max_boot_attempts;
    uint8_t rollback_reason;
    uint8_t reserved0;
    uint32_t loader_size;
    uint32_t slot_size;
    BootImageInfo images[2];
} BootControlState;

typedef struct {
    BootSlot slot;
    uint32_t flash_offset;
    uint32_t max_size;
    const char *name;
} BootSlotRegion;

#define BOOTCTL_WATCHDOG_HANDOFF_MAGIC         0x42544F54u
#define BOOTCTL_WATCHDOG_HANDOFF_MAGIC_SCRATCH 4u
#define BOOTCTL_WATCHDOG_HANDOFF_SLOT_SCRATCH  5u
#define BOOTCTL_WATCHDOG_KEEP_ESP_MAGIC        0x4553504Bu
#define BOOTCTL_WATCHDOG_KEEP_ESP_SCRATCH      6u
#define BOOTCTL_RECOVERY_MENU_MAGIC            0x4D454E55u
#define BOOTCTL_RECOVERY_MENU_SCRATCH          7u

void bootctl_defaults(BootControlState *out);
bool bootctl_load(BootControlState *out);
bool bootctl_store(const BootControlState *state);

const BootSlotRegion *bootctl_slot_region(BootSlot slot);
BootSlot bootctl_active_slot(const BootControlState *state);
BootSlot bootctl_inactive_slot(const BootControlState *state);
BootSlot bootctl_select_boot_slot(const BootControlState *state);
bool bootctl_slot_can_fit(BootSlot slot, uint32_t image_size);
bool bootctl_slot_has_bootable_image(const BootControlState *state, BootSlot slot);

bool bootctl_stage_update(BootControlState *state,
                          BootSlot slot,
                          uint32_t image_size,
                          uint32_t image_crc32,
                          const char *version_tag);
bool bootctl_mark_boot_attempt(BootControlState *state, BootSlot slot);
bool bootctl_mark_confirmed(BootControlState *state, BootSlot slot);
bool bootctl_mark_rollback(BootControlState *state,
                           BootSlot fallback_slot,
                           BootRollbackReason reason);
bool bootctl_clear_slot(BootControlState *state, BootSlot slot);
