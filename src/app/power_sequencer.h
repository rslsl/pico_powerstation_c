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
//   1. pseq_latch()        - assert SYSTEM_HOLD immediately, keep other relays off
//   2. _init_all()
//   3. _startup_validate()
//   4. pseq_resolve()      -> BootMode
//
// Power-off:
//   pseq_self_off()        - disable loads first, then release SYSTEM_HOLD
//   pseq_user_poweroff()   - same, with shutdown tone / grace period

typedef enum {
    BOOT_NORMAL = 0,
    BOOT_CHARGE_ONLY,
    BOOT_DIAGNOSTIC,
} BootMode;

typedef struct {
    BootMode mode;
    bool     latched;
} PowerSeq;

// Step 1: assert SYSTEM_HOLD and put all other relay GPIO in safe state.
void     pseq_latch(PowerSeq *ps);

// Step 2: resolve boot mode after startup validation.
BootMode pseq_resolve(PowerSeq *ps, bool startup_ok, float soc_ocv);

// Power-off path. Does not return.
void     pseq_self_off(PowerSeq *ps, const char *msg);

// User-requested power-off with short grace period.
void     pseq_user_poweroff(PowerSeq *ps, Buzzer *bz);
