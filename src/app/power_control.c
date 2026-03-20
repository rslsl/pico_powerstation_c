// ============================================================
// app/power_control.c
//
// v5.0 changes:
//   FMEA-01 - explicit PowerPolicy
//   FMEA-04 - OCP handling in protection.c
//   Power-seq - pwr_init() inherits SYSTEM_HOLD from pseq_latch()
// ============================================================
#include "power_control.h"
#include "../config.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>

#define GPIO_UNUSED 0xFFu

static const uint8_t _GPIO[PORT_COUNT] = {
    GPIO_DC_OUT, GPIO_PWR_LATCH, GPIO_USB_PD, GPIO_FAN, GPIO_UNUSED
};
static const char *_NAMES[PORT_COUNT] = {
    "DC OUT", "System Hold", "USB-PD", "Fan", "Charge Sense"
};

static bool _port_is_switchable(PortId p) {
    return p != PORT_CHARGE;
}

static bool _port_blocked_in_safe_mode(PortId p) {
    return p == PORT_DC_OUT || p == PORT_USB_PD;
}

void pwr_init(PowerControl *pwr) {
    memset(pwr, 0, sizeof(*pwr));
    memcpy(pwr->gpio, _GPIO, sizeof(_GPIO));
    pwr->policy = PWR_POLICY_ISOLATED;

    for (int i = 0; i < PORT_COUNT; i++)
        pwr->state[i] = false;

    // pseq_latch() has already asserted the hold relay before full init starts.
    pwr->state[PORT_SYSTEM_HOLD] = true;
    printf("[PWR] init: SYSTEM_HOLD ON, other ports OFF, policy=ISOLATED\n");
}

bool pwr_policy_set(PowerControl *pwr, PowerPolicy policy) {
    pwr->policy = policy;
    return true;
}

PowerPolicy pwr_policy_get(const PowerControl *pwr) {
    return pwr->policy;
}

// Apply a policy to port states.
// Charge input is sensor-only in this hardware revision.
void pwr_apply_policy(PowerControl *pwr, PowerPolicy policy) {
    pwr_policy_set(pwr, policy);

    switch (policy) {
    case PWR_POLICY_ISOLATED:
    case PWR_POLICY_FAULT_LATCH:
        pwr_enable(pwr, PORT_SYSTEM_HOLD);
        pwr_disable(pwr, PORT_DC_OUT);
        pwr_disable(pwr, PORT_USB_PD);
        pwr_disable(pwr, PORT_FAN);
        printf("[PWR] policy -> %s\n",
               policy == PWR_POLICY_ISOLATED ? "ISOLATED" : "FAULT_LATCH");
        break;

    case PWR_POLICY_CHARGE_ONLY:
        pwr_enable(pwr, PORT_SYSTEM_HOLD);
        pwr_disable(pwr, PORT_DC_OUT);
        pwr_disable(pwr, PORT_USB_PD);
        pwr_disable(pwr, PORT_FAN);
        printf("[PWR] policy -> CHARGE_ONLY (%s)\n",
               pwr->charger_present ? "passive input present" : "waiting for charger");
        break;

    case PWR_POLICY_LOADS_ON:
        pwr_enable(pwr, PORT_SYSTEM_HOLD);
        pwr_enable(pwr, PORT_DC_OUT);
        pwr_enable(pwr, PORT_USB_PD);
        pwr_disable(pwr, PORT_FAN);
        printf("[PWR] policy -> LOADS_ON (input:%s)\n",
               pwr->charger_present ? "yes" : "no");
        break;
    }
}

void pwr_enable(PowerControl *pwr, PortId p) {
    if (!_port_is_switchable(p)) {
        pwr->state[p] = false;
        return;
    }
    if (pwr->safe_mode && _port_blocked_in_safe_mode(p)) {
        printf("[PWR] BLOCKED safe_mode: %s\n", _NAMES[p]);
        return;
    }
    gpio_put(pwr->gpio[p], MOSFET_ON);
    pwr->state[p] = true;
}

void pwr_disable(PowerControl *pwr, PortId p) {
    if (!_port_is_switchable(p)) {
        pwr->state[p] = false;
        return;
    }
    gpio_put(pwr->gpio[p], MOSFET_OFF);
    pwr->state[p] = false;
}

void pwr_toggle(PowerControl *pwr, PortId p) {
    if (pwr->state[p]) pwr_disable(pwr, p);
    else               pwr_enable(pwr, p);
}

bool pwr_is_on(const PowerControl *pwr, PortId p) {
    return pwr->state[p];
}

// Disable all high-power relays while keeping SYSTEM_HOLD active.
void pwr_emergency_off(PowerControl *pwr) {
    printf("[PWR] EMERGENCY OFF - all outputs\n");
    pwr_disable(pwr, PORT_DC_OUT);
    pwr_enable(pwr, PORT_SYSTEM_HOLD);
    pwr_disable(pwr, PORT_USB_PD);
    pwr_disable(pwr, PORT_FAN);
    pwr_policy_set(pwr, PWR_POLICY_FAULT_LATCH);
}

void pwr_set_safe_mode(PowerControl *pwr, bool on) {
    if (pwr->safe_mode == on) return;
    pwr->safe_mode = on;
    if (on) {
        printf("[PWR] safe_mode -> loads locked\n");
        pwr_enable(pwr, PORT_SYSTEM_HOLD);
        pwr_disable(pwr, PORT_DC_OUT);
        pwr_disable(pwr, PORT_USB_PD);
    } else {
        printf("[PWR] safe_mode cleared\n");
    }
}

bool pwr_is_safe_mode(const PowerControl *pwr) { return pwr->safe_mode; }

void pwr_set_charger_present(PowerControl *pwr, bool present) {
    bool changed = (pwr->charger_present != present);
    pwr->charger_present = present;
    if (changed) {
        printf("[PWR] charger_present = %s\n", present ? "YES" : "NO");
    }
}

bool pwr_is_charger_present(const PowerControl *pwr) {
    return pwr->charger_present;
}

const char *pwr_name(PortId p) { return _NAMES[p]; }

void pwr_set_charge_inhibit(PowerControl *pwr, bool inhibit) {
    if (pwr->charge_inhibit == inhibit) return;
    pwr->charge_inhibit = inhibit;
    printf("[PWR] charge_inhibit advisory = %s (no charge relay)\n", inhibit ? "YES" : "NO");
}

bool pwr_is_charge_inhibited(const PowerControl *pwr) {
    return pwr->charge_inhibit;
}
