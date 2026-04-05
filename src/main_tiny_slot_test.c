// ============================================================
// main_tiny_slot_test.c - Minimal slot app for OTA boot validation
// ============================================================
// Purpose: Verify the OTA loader can chainload into a slot image.
// This app does ONLY:
//   - early USB console log (proof of life)
//   - GPIO heartbeat (LED or power latch blink pattern)
//   - boot_control self-confirm
// No display, no I2C, no ESP, no core1.
// ============================================================

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"

#include "config.h"
#include "app/boot_control.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef PICO_OTA_CURRENT_SLOT
#define PICO_OTA_CURRENT_SLOT 0
#endif

#define HEARTBEAT_MS 500u
#ifndef TINY_TEST_BOOTSEL_AFTER_LOOPS
#define TINY_TEST_BOOTSEL_AFTER_LOOPS 20u
#endif

// Use the onboard LED (GP25 on standard Pico) for heartbeat.
// If the board has no LED on GP25, this is still safe -- just a no-op pin toggle.
#define HEARTBEAT_GPIO 25u

static void _confirm_slot(void) {
#if (PICO_OTA_CURRENT_SLOT == 1) || (PICO_OTA_CURRENT_SLOT == 2)
    BootControlState state;
    BootSlot slot = (PICO_OTA_CURRENT_SLOT == 1) ? BOOT_SLOT_A : BOOT_SLOT_B;
    bool loaded = bootctl_load(&state);

    if (!loaded) {
        extern uint8_t __flash_binary_start;
        extern uint8_t __flash_binary_end;
        uint32_t image_size = (uint32_t)(&__flash_binary_end - &__flash_binary_start);

        bootctl_defaults(&state);
        if (!bootctl_stage_update(&state, slot, image_size, 0u, FW_VERSION)) {
            printf("[TINY] stage failed\n");
            return;
        }
    }

    if (bootctl_mark_confirmed(&state, slot) && bootctl_store(&state)) {
        printf("[TINY] confirmed %s\n",
               slot == BOOT_SLOT_A ? "slot-a" : "slot-b");
    } else {
        printf("[TINY] confirm failed\n");
    }
#else
    printf("[TINY] monolithic build, no slot confirm\n");
#endif
}

int main(void) {
    stdio_init_all();

    // Brief settle for USB enumeration
    sleep_ms(1500);

    printf("\n========================================\n");
    printf("  Pico OTA Tiny Slot Test  %s\n", FW_VERSION);
    printf("  Slot: %d\n", PICO_OTA_CURRENT_SLOT);
    printf("========================================\n");

    gpio_init(HEARTBEAT_GPIO);
    gpio_set_dir(HEARTBEAT_GPIO, GPIO_OUT);

    _confirm_slot();

    printf("[TINY] entering heartbeat loop\n");

    bool led_state = false;
    uint32_t loop_count = 0u;

    while (1) {
        led_state = !led_state;
        gpio_put(HEARTBEAT_GPIO, led_state);

        if ((loop_count % 10u) == 0u) {
            printf("[TINY] heartbeat #%lu\n", (unsigned long)loop_count);
        }
        loop_count++;

#if TINY_TEST_BOOTSEL_AFTER_LOOPS > 0u
        if (loop_count >= TINY_TEST_BOOTSEL_AFTER_LOOPS) {
            printf("[TINY] returning to BOOTSEL for validation\n");
            sleep_ms(50);
            reset_usb_boot(0u, 0u);
        }
#endif

        sleep_ms(HEARTBEAT_MS);
    }
}
