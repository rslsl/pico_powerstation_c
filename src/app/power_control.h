#pragma once
// ============================================================
// app/power_control.h
//
// v5.0 changes:
//   FMEA-01 - explicit PowerPolicy enum
//   FMEA-04 - OCP cuts all discharge outputs
//   Power-seq - pwr_init() inherits SYSTEM_HOLD from pseq_latch()
// ============================================================
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PORT_DC_OUT      = 0,
    PORT_SYSTEM_HOLD = 1,
    PORT_USB_PD      = 2,
    PORT_FAN         = 3,
    PORT_CHARGE      = 4,  // telemetry-only charge input, not switchable
    PORT_COUNT
} PortId;

typedef enum {
    PWR_POLICY_ISOLATED    = 0,
    PWR_POLICY_CHARGE_ONLY = 1,
    PWR_POLICY_LOADS_ON    = 2,
    PWR_POLICY_FAULT_LATCH = 3,
} PowerPolicy;

typedef struct PowerControl {
    uint8_t      gpio[PORT_COUNT];
    bool         state[PORT_COUNT];
    bool         desired_state[PORT_COUNT];
    bool         safe_mode;
    PowerPolicy  policy;
    bool         charger_present;
    bool         charge_inhibit;  // advisory flag only in this HW revision
    uint32_t     persist_seq;
    uint8_t      persist_slot;
    bool         persist_ready;
} PowerControl;

void pwr_init(PowerControl *pwr);

bool pwr_policy_set(PowerControl *pwr, PowerPolicy policy);
PowerPolicy pwr_policy_get(const PowerControl *pwr);

// Configure relay states to match a high-level policy.
void pwr_apply_policy(PowerControl *pwr, PowerPolicy policy);

// Low-level relay control.
void pwr_enable (PowerControl *pwr, PortId p);
void pwr_disable(PowerControl *pwr, PortId p);
void pwr_toggle (PowerControl *pwr, PortId p);
bool pwr_is_on  (const PowerControl *pwr, PortId p);
bool pwr_user_desired_on(const PowerControl *pwr, PortId p);

// User-intent relay control with persistence across power cycles.
void pwr_user_set   (PowerControl *pwr, PortId p, bool on);
void pwr_user_toggle(PowerControl *pwr, PortId p);
void pwr_restore_user_state(PowerControl *pwr);

// Disable all load relays while keeping SYSTEM_HOLD active.
void pwr_emergency_off(PowerControl *pwr);

void pwr_set_safe_mode(PowerControl *pwr, bool on);
bool pwr_is_safe_mode(const PowerControl *pwr);

void pwr_set_charger_present(PowerControl *pwr, bool present);
bool pwr_is_charger_present(const PowerControl *pwr);

const char *pwr_name(PortId p);

void pwr_set_charge_inhibit(PowerControl *pwr, bool inhibit);
bool pwr_is_charge_inhibited(const PowerControl *pwr);
