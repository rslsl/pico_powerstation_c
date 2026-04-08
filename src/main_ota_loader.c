#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "hardware/address_mapped.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/sync.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/resets.h"

#include "config.h"
#include "app/boot_control.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define LOADER_VECTOR_OFFSET 0x100u
#define LOADER_RAM_START     0x20000000u
#define LOADER_RAM_END       0x20042000u
#define LOADER_RECOVERY_SAMPLE_COUNT 12u
#define LOADER_RECOVERY_SAMPLE_DELAY_MS 10u

#ifndef PICO_OTA_LOADER_USB_DEBUG
#define PICO_OTA_LOADER_USB_DEBUG 0
#endif

#if PICO_OTA_LOADER_USB_DEBUG
#include "pico/stdio_usb.h"
#define LOADER_USB_WAIT_MS 5000u
#define LOADER_LOG(...) printf(__VA_ARGS__)
#else
#define LOADER_LOG(...) ((void)0)
#endif

static int _slot_index(BootSlot slot) {
    if (slot == BOOT_SLOT_A) return 0;
    if (slot == BOOT_SLOT_B) return 1;
    return -1;
}

static void _loader_button_init(void) {
    gpio_init(BTN_DOWN_PIN);
    gpio_set_dir(BTN_DOWN_PIN, GPIO_IN);
    gpio_pull_up(BTN_DOWN_PIN);
}

static bool _loader_recovery_requested(void) {
    uint32_t pressed = 0u;

    _loader_button_init();
    sleep_ms(20);
    for (uint32_t i = 0u; i < LOADER_RECOVERY_SAMPLE_COUNT; ++i) {
        if (gpio_get(BTN_DOWN_PIN) == 0) {
            ++pressed;
        }
        sleep_ms(LOADER_RECOVERY_SAMPLE_DELAY_MS);
    }
    return pressed >= 9u;
}

static BootSlot _consume_watchdog_handoff_slot(void) {
    uint32_t magic = watchdog_hw->scratch[BOOTCTL_WATCHDOG_HANDOFF_MAGIC_SCRATCH];
    uint32_t raw_slot = watchdog_hw->scratch[BOOTCTL_WATCHDOG_HANDOFF_SLOT_SCRATCH];

    watchdog_hw->scratch[BOOTCTL_WATCHDOG_HANDOFF_MAGIC_SCRATCH] = 0u;
    watchdog_hw->scratch[BOOTCTL_WATCHDOG_HANDOFF_SLOT_SCRATCH] = 0u;

    if (magic != BOOTCTL_WATCHDOG_HANDOFF_MAGIC) return BOOT_SLOT_NONE;
    if (raw_slot == (uint32_t)BOOT_SLOT_A || raw_slot == (uint32_t)BOOT_SLOT_B) {
        return (BootSlot)raw_slot;
    }
    return BOOT_SLOT_NONE;
}

static bool _watchdog_keep_esp_requested(void) {
    return watchdog_hw->scratch[BOOTCTL_WATCHDOG_KEEP_ESP_SCRATCH] == BOOTCTL_WATCHDOG_KEEP_ESP_MAGIC;
}

static void _set_recovery_menu_requested(bool requested) {
    watchdog_hw->scratch[BOOTCTL_RECOVERY_MENU_SCRATCH] =
        requested ? BOOTCTL_RECOVERY_MENU_MAGIC : 0u;
}

static void _loader_keep_esp_power(bool keep_power) {
    if (!keep_power) return;
    gpio_init(GPIO_ESP_EN);
    gpio_set_dir(GPIO_ESP_EN, GPIO_OUT);
    gpio_put(GPIO_ESP_EN, ESP_EN_ON);
}

#if PICO_OTA_LOADER_USB_DEBUG
static const char *_slot_name(BootSlot slot) {
    if (slot == BOOT_SLOT_A) return "slot-a";
    if (slot == BOOT_SLOT_B) return "slot-b";
    return "none";
}
#endif

static const BootImageInfo *_slot_image(const BootControlState *state, BootSlot slot) {
    int idx = _slot_index(slot);
    if (!state || idx < 0) return NULL;
    return &state->images[idx];
}

#if PICO_OTA_LOADER_USB_DEBUG
static void _wait_for_usb_briefly(void) {
    uint32_t start_ms = to_ms_since_boot(get_absolute_time());
    while (!stdio_usb_connected()) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if ((now_ms - start_ms) >= LOADER_USB_WAIT_MS) break;
        sleep_ms(10);
    }
}
#endif

static bool _slot_vectors_valid(BootSlot slot,
                                uint32_t *vector_addr_out,
                                uint32_t *sp_out,
                                uint32_t *pc_out) {
    const BootSlotRegion *region = bootctl_slot_region(slot);
    uint32_t vector_addr;
    uint32_t sp;
    uint32_t pc;
    uint32_t pc_min;
    uint32_t pc_max;
    bool sp_ok;
    bool pc_ok;

    if (!region) return false;

    vector_addr = XIP_BASE + region->flash_offset + LOADER_VECTOR_OFFSET;
    sp = *(const uint32_t *)(uintptr_t)vector_addr;
    pc = *(const uint32_t *)(uintptr_t)(vector_addr + 4u);
    pc_min = XIP_BASE + region->flash_offset + LOADER_VECTOR_OFFSET;
    pc_max = XIP_BASE + region->flash_offset + region->max_size;

    sp_ok = (sp >= LOADER_RAM_START) && (sp <= LOADER_RAM_END) && ((sp & 0x3u) == 0u);
    pc_ok = ((pc & 0x1u) != 0u) && (pc >= pc_min) && (pc < pc_max);
    if (!sp_ok || !pc_ok) return false;

    if (vector_addr_out) *vector_addr_out = vector_addr;
    if (sp_out) *sp_out = sp;
    if (pc_out) *pc_out = pc;
    return true;
}

static BootSlot _probe_bootable_slot(bool prefer_recovery) {
    if (prefer_recovery) {
        if (_slot_vectors_valid(BOOT_SLOT_A, NULL, NULL, NULL)) return BOOT_SLOT_A;
        if (_slot_vectors_valid(BOOT_SLOT_B, NULL, NULL, NULL)) return BOOT_SLOT_B;
    } else {
        if (_slot_vectors_valid(BOOT_SLOT_B, NULL, NULL, NULL)) return BOOT_SLOT_B;
        if (_slot_vectors_valid(BOOT_SLOT_A, NULL, NULL, NULL)) return BOOT_SLOT_A;
    }
    return BOOT_SLOT_NONE;
}

static BootSlot _select_main_slot(const BootControlState *state) {
    const BootImageInfo *pending_image;

    if (!state) return BOOT_SLOT_NONE;

    if ((BootSlot)state->pending_slot == BOOT_SLOT_B) {
        pending_image = _slot_image(state, BOOT_SLOT_B);
        if (pending_image &&
            bootctl_slot_has_bootable_image(state, BOOT_SLOT_B) &&
            pending_image->boot_attempts < state->max_boot_attempts) {
            return BOOT_SLOT_B;
        }
    }

    if (bootctl_slot_has_bootable_image(state, BOOT_SLOT_B)) {
        return BOOT_SLOT_B;
    }
    return BOOT_SLOT_NONE;
}

static BootSlot _fallback_slot(const BootControlState *state, BootSlot avoid) {
    BootSlot confirmed;
    BootSlot active;

    if (!state) return BOOT_SLOT_NONE;

    confirmed = (BootSlot)state->confirmed_slot;
    if (confirmed != avoid && bootctl_slot_has_bootable_image(state, confirmed)) {
        return confirmed;
    }

    active = bootctl_active_slot(state);
    if (active != avoid && bootctl_slot_has_bootable_image(state, active)) {
        return active;
    }

    if (avoid != BOOT_SLOT_A && bootctl_slot_has_bootable_image(state, BOOT_SLOT_A)) {
        return BOOT_SLOT_A;
    }
    if (avoid != BOOT_SLOT_B && bootctl_slot_has_bootable_image(state, BOOT_SLOT_B)) {
        return BOOT_SLOT_B;
    }
    return BOOT_SLOT_NONE;
}

static void _maybe_rollback_exhausted_pending(BootControlState *state) {
    BootSlot pending;
    const BootImageInfo *image;
    BootSlot fallback;

    if (!state) return;
    pending = (BootSlot)state->pending_slot;
    image = _slot_image(state, pending);
    if (!image) return;
    if (image->boot_attempts < state->max_boot_attempts) return;

    fallback = _fallback_slot(state, pending);
    LOADER_LOG("[LOADER] pending %s exceeded boot limit (%lu/%u)\n",
               _slot_name(pending),
               (unsigned long)image->boot_attempts,
               (unsigned)state->max_boot_attempts);
    if (bootctl_mark_rollback(state, fallback, BOOT_ROLLBACK_BOOT_LIMIT)) {
        bootctl_store(state);
    }
}

static void _mark_pending_boot_attempt(BootControlState *state, BootSlot slot) {
    const BootImageInfo *image;

    if (!state || slot != (BootSlot)state->pending_slot) return;
    if (!bootctl_mark_boot_attempt(state, slot)) return;
    image = _slot_image(state, slot);
    if (bootctl_store(state) && image) {
        LOADER_LOG("[LOADER] boot attempt %lu for %s\n",
                   (unsigned long)image->boot_attempts,
                   _slot_name(slot));
    }
}

static void _mark_bad_pending_image(BootControlState *state, BootSlot slot) {
    BootSlot fallback;

    if (!state || slot != (BootSlot)state->pending_slot) return;
    fallback = _fallback_slot(state, slot);
    bootctl_clear_slot(state, slot);
    if (bootctl_mark_rollback(state, fallback, BOOT_ROLLBACK_BAD_IMAGE)) {
        bootctl_store(state);
    }
}

static void __attribute__((noreturn)) _jump_to_slot(uint32_t vector_addr,
                                                    uint32_t sp,
                                                    uint32_t pc) {
    (void)save_and_disable_interrupts();

    // Disable all NVIC IRQs and clear any pending.
    irq_set_mask_enabled(0xFFFFFFFFu, false);
    // NVIC ICPR: write 1-bits to clear pending IRQs (Cortex-M0+ has one 32-bit register).
    *(volatile uint32_t *)0xE000E280u = 0xFFFFFFFFu;
    // Clear pending system exceptions to emulate a clean reset handoff.
    *(volatile uint32_t *)0xE000ED04u = (1u << 27) | (1u << 25); // ICSR: PENDSVCLR | PENDSTCLR

    // Stop SysTick so the slot app CRT0 starts from a clean timer state.
    *(volatile uint32_t *)0xE000E010u = 0u; // SYST_CSR = 0

    // Reset peripherals the loader may have touched (timer, IO, pads)
    // so the slot app's runtime_init() can reinitialize them cleanly.
    // USBCTRL is explicitly included to guarantee a clean USB handoff.
    reset_block(RESETS_RESET_TIMER_BITS |
                RESETS_RESET_IO_BANK0_BITS |
                RESETS_RESET_PADS_BANK0_BITS |
                RESETS_RESET_USBCTRL_BITS);
    unreset_block_wait(RESETS_RESET_TIMER_BITS |
                       RESETS_RESET_IO_BANK0_BITS |
                       RESETS_RESET_PADS_BANK0_BITS);
    // USBCTRL left in reset intentionally -- slot app will unreset it.

    scb_hw->vtor = vector_addr;
    __asm volatile("" ::: "memory");
    __asm volatile(
        "movs r0, #0\n"
        "msr control, r0\n"
        "msr primask, r0\n"
        "isb\n"
        "msr msp, %0\n"
        "bx %1\n"
        :
        : "r"(sp), "r"(pc)
        : "r0", "memory"
    );
    __builtin_unreachable();
}

int main(void) {
    BootControlState state;
    bool have_state;
    bool recovery_requested;
    bool keep_esp_power;
    BootSlot forced_slot;
    BootSlot slot;
    uint32_t vector_addr = 0u;
    uint32_t sp = 0u;
    uint32_t pc = 0u;

#if PICO_OTA_LOADER_USB_DEBUG
    stdio_init_all();
    _wait_for_usb_briefly();
    LOADER_LOG("\n=== Pico OTA loader %s ===\n", FW_VERSION);
#endif

    forced_slot = _consume_watchdog_handoff_slot();
    keep_esp_power = _watchdog_keep_esp_requested();
    _loader_keep_esp_power(keep_esp_power);
    _set_recovery_menu_requested(false);
    recovery_requested = _loader_recovery_requested();
    LOADER_LOG("[LOADER] recovery request=%u\n", recovery_requested ? 1u : 0u);
    LOADER_LOG("[LOADER] forced slot=%s\n", _slot_name(forced_slot));

    have_state = bootctl_load(&state);
    if (have_state) {
        _maybe_rollback_exhausted_pending(&state);
        if (forced_slot != BOOT_SLOT_NONE) {
            slot = forced_slot;
        } else if (recovery_requested) {
            slot = BOOT_SLOT_A;
            _set_recovery_menu_requested(true);
        } else {
            slot = _select_main_slot(&state);
        }
    } else {
        bootctl_defaults(&state);
        slot = forced_slot;
        LOADER_LOG("[LOADER] boot control empty, probing slots directly\n");
    }

    // Validate vectors BEFORE incrementing boot attempt counter
    if (slot != BOOT_SLOT_NONE && !_slot_vectors_valid(slot, &vector_addr, &sp, &pc)) {
        LOADER_LOG("[LOADER] %s has invalid vectors\n", _slot_name(slot));
        _mark_bad_pending_image(&state, slot);
        slot = BOOT_SLOT_NONE;
    }

    // Only increment boot attempt after confirming valid vectors
    if (have_state && slot != BOOT_SLOT_NONE && !recovery_requested) {
        _mark_pending_boot_attempt(&state, slot);
    }

    if (slot == BOOT_SLOT_NONE) {
        slot = _probe_bootable_slot(recovery_requested);
        if (slot != BOOT_SLOT_NONE) {
            _slot_vectors_valid(slot, &vector_addr, &sp, &pc);
            LOADER_LOG("[LOADER] fallback probe selected %s\n", _slot_name(slot));
        }
    }

    if (slot != BOOT_SLOT_NONE) {
#if PICO_OTA_LOADER_USB_DEBUG
        LOADER_LOG("[LOADER] jumping to %s @ 0x%08lx\n",
                   _slot_name(slot),
                   (unsigned long)vector_addr);
        sleep_ms(20);
#endif
        _jump_to_slot(vector_addr, sp, pc);
    }

    LOADER_LOG("[LOADER] no bootable app slot, entering BOOTSEL\n");
#if PICO_OTA_LOADER_USB_DEBUG
    sleep_ms(100);
#endif
    reset_usb_boot(0u, 0u);
}
