// ============================================================
// app/power_sequencer.c - system power hold + self-off
// ============================================================
#include "power_sequencer.h"
#include "power_control.h"
#include "../config.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

void pseq_latch(PowerSeq *ps) {
    memset(ps, 0, sizeof(*ps));

    // Keep every relay inactive first so power rails do not short immediately
    // on boot while the controller waits out the bootstrap delay.
    gpio_init(GPIO_PWR_LATCH);
    gpio_set_dir(GPIO_PWR_LATCH, GPIO_OUT);
    gpio_put(GPIO_PWR_LATCH, MOSFET_OFF);

    // Bring the remaining relays into a safe inactive state.
    const uint8_t relays[] = {
        GPIO_DC_OUT, GPIO_USB_PD, GPIO_FAN
    };
    for (size_t i = 0; i < (sizeof(relays) / sizeof(relays[0])); i++) {
        gpio_init(relays[i]);
        gpio_set_dir(relays[i], GPIO_OUT);
        gpio_put(relays[i], MOSFET_OFF);
    }

    // Delay before asserting SYSTEM_HOLD so the bootstrap relay does not
    // immediately close the power line on input power application.
    sleep_ms(PWR_BOOTSTRAP_DELAY_MS);

    // Assert the hold relay so the board can keep itself powered.
    gpio_put(GPIO_PWR_LATCH, MOSFET_ON);
    sleep_ms(PWR_HOLD_ASSERT_MS);

    ps->latched = true;
}

BootMode pseq_resolve(PowerSeq *ps, bool startup_ok, float soc_ocv, bool ota_safe_requested) {
    if (ota_safe_requested) {
        ps->mode = BOOT_OTA_SAFE;
    } else if (!startup_ok) {
        ps->mode = BOOT_DIAGNOSTIC;
    } else if (soc_ocv < 5.0f) {
        ps->mode = BOOT_CHARGE_ONLY;
    } else {
        ps->mode = BOOT_NORMAL;
    }
    printf("[SEQ] boot mode: %d (ok=%d soc=%.1f%% ota_safe=%d)\n",
           ps->mode, startup_ok, soc_ocv, ota_safe_requested ? 1 : 0);
    return ps->mode;
}

void pseq_self_off(PowerSeq *ps, const char *msg) {
    printf("[SEQ] self-off: %s\n", msg ? msg : "");

    // Drop load relays first so the hold relay is the final power path.
    const uint8_t loads[] = {
        GPIO_DC_OUT, GPIO_USB_PD, GPIO_FAN
    };
    for (size_t i = 0; i < (sizeof(loads) / sizeof(loads[0])); i++)
        gpio_put(loads[i], MOSFET_OFF);

    sleep_ms(PWR_HOLD_RELEASE_MS);
    gpio_put(GPIO_PWR_LATCH, MOSFET_OFF);
    if (ps) ps->latched = false;

    printf("[SEQ] SYSTEM_HOLD released\n");
    while (1) tight_loop_contents();
}

void pseq_user_poweroff(PowerSeq *ps, Buzzer *bz) {
    (void)bz;
    printf("[SEQ] user power-off\n");
    pseq_self_off(ps, NULL);
}
