#pragma once
// ============================================================
// app/power_sequencer.h - relay safe-start + physical power hold
// ============================================================
#include "buzzer.h"
#include <stdbool.h>
#include <stdint.h>

// Key power GPIO:
//   GPIO_PWR_LATCH (GP7)  - active-low SYSTEM_HOLD relay.
//   GPIO_DC_OUT    (GP4)  - active-low DC output relay.
//
// Boot sequence:
//   1. pseq_latch()        - keep relays OFF, wait bootstrap delay, assert SYSTEM_HOLD
//   2. _init_all()
//   3. _startup_validate()
//   4. pseq_resolve()      -> BootMode
//
// Power-off:
//   pseq_self_off()        - disable loads first, then release SYSTEM_HOLD
//   pseq_user_poweroff()   - same, after UI countdown/audio sequence completes

typedef enum {
    BOOT_NORMAL = 0,
    BOOT_CHARGE_ONLY,
    BOOT_DIAGNOSTIC,
    BOOT_OTA_SAFE,
} BootMode;

typedef struct {
    BootMode mode;
    bool     latched;
} PowerSeq;

// Step 1: keep relay GPIO in a safe OFF state, then assert SYSTEM_HOLD after delay.
void     pseq_latch(PowerSeq *ps);

// Step 2: resolve boot mode after startup validation.
BootMode pseq_resolve(PowerSeq *ps, bool startup_ok, float soc_ocv, bool ota_safe_requested);

// Power-off path. Does not return.
void     pseq_self_off(PowerSeq *ps, const char *msg);

// User-requested power-off after the UI finishes its countdown sequence.
void     pseq_user_poweroff(PowerSeq *ps, Buzzer *bz);
