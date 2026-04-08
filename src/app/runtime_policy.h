#pragma once

#include <stdbool.h>

#define RUNTIME_CHARGER_PRESENT_MIN_A      0.05f
#define RUNTIME_CHARGE_ENTER_CURRENT_A     0.30f
#define RUNTIME_CHARGE_ENTER_NET_A        -0.15f
#define RUNTIME_CHARGE_HOLD_CURRENT_A      0.20f
#define RUNTIME_CHARGE_HOLD_NET_A         -0.05f

#define RUNTIME_DISCHARGE_ENTER_CURRENT_A  0.50f
#define RUNTIME_DISCHARGE_ENTER_POWER_W    5.0f
#define RUNTIME_DISCHARGE_HOLD_CURRENT_A   0.40f
#define RUNTIME_DISCHARGE_HOLD_POWER_W     3.0f
#define RUNTIME_DISCHARGE_MIN_MS          30000u

#define RUNTIME_CHARGER_RECHECK_MS         5000u
#define RUNTIME_I2C_RECOVERY_STREAK        3u

static inline bool runtime_policy_charge_enter(float i_chg, float i_net) {
    return (i_chg > RUNTIME_CHARGE_ENTER_CURRENT_A) &&
           (i_net < RUNTIME_CHARGE_ENTER_NET_A);
}

static inline bool runtime_policy_charge_hold(float i_chg, float i_net) {
    return (i_chg > RUNTIME_CHARGE_HOLD_CURRENT_A) &&
           (i_net < RUNTIME_CHARGE_HOLD_NET_A);
}

static inline bool runtime_policy_charger_present(float i_chg) {
    return i_chg > RUNTIME_CHARGER_PRESENT_MIN_A;
}

static inline bool runtime_policy_discharge_enter(bool is_charging,
                                                  float i_net,
                                                  float power_w) {
    return !is_charging &&
           i_net > RUNTIME_DISCHARGE_ENTER_CURRENT_A &&
           power_w > RUNTIME_DISCHARGE_ENTER_POWER_W;
}

static inline bool runtime_policy_discharge_hold(bool is_charging,
                                                 float i_net,
                                                 float power_w) {
    return !is_charging &&
           i_net > RUNTIME_DISCHARGE_HOLD_CURRENT_A &&
           power_w > RUNTIME_DISCHARGE_HOLD_POWER_W;
}
