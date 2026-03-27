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
#include "../bms/flash_nvm.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>

#define GPIO_UNUSED 0xFFu
#define RELAY_STATE_MAGIC 0x524C5931u
#define RELAY_STATE_VERSION 1u

static const uint8_t _GPIO[PORT_COUNT] = {
    GPIO_DC_OUT, GPIO_PWR_LATCH, GPIO_USB_PD, GPIO_FAN, GPIO_UNUSED
};
static const char *_NAMES[PORT_COUNT] = {
    "DC OUT", "System Hold", "USB-PD", "Fan", "Charge Sense"
};

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t mask;
    uint8_t _pad[2];
} RelayStatePayload;

static bool _port_is_switchable(PortId p) {
    return p != PORT_CHARGE;
}

static bool _port_blocked_in_safe_mode(PortId p) {
    return p == PORT_DC_OUT || p == PORT_USB_PD;
}

static bool _port_is_user_persisted(PortId p) {
    return p == PORT_DC_OUT || p == PORT_USB_PD || p == PORT_FAN;
}

static uint8_t _persist_mask(const PowerControl *pwr) {
    uint8_t mask = 0u;
    if (pwr->desired_state[PORT_DC_OUT]) mask |= (1u << 0);
    if (pwr->desired_state[PORT_USB_PD]) mask |= (1u << 1);
    if (pwr->desired_state[PORT_FAN])    mask |= (1u << 2);
    return mask;
}

static void _desired_from_mask(PowerControl *pwr, uint8_t mask) {
    pwr->desired_state[PORT_DC_OUT] = (mask & (1u << 0)) != 0;
    pwr->desired_state[PORT_USB_PD] = (mask & (1u << 1)) != 0;
    pwr->desired_state[PORT_FAN]    = (mask & (1u << 2)) != 0;
}

static void _desired_defaults(PowerControl *pwr) {
    memset(pwr->desired_state, 0, sizeof(pwr->desired_state));
    pwr->desired_state[PORT_DC_OUT] = true;
    pwr->desired_state[PORT_USB_PD] = true;
    pwr->desired_state[PORT_FAN] = false;
}

static void _persist_load(PowerControl *pwr) {
    RelayStatePayload payload = {0};

    pwr->persist_seq = 0;
    pwr->persist_slot = 0;
    _desired_defaults(pwr);

    if (nvm_ab_load(FLASH_RELAY_OFFSET, FLASH_RELAY_OFFSET_B,
                    RELAY_STATE_MAGIC,
                    &payload, sizeof(payload),
                    &pwr->persist_seq, &pwr->persist_slot) &&
        payload.version == RELAY_STATE_VERSION) {
        _desired_from_mask(pwr, payload.mask);
        printf("[PWR] restored relay mask=0x%02X seq=%lu\n",
               payload.mask, (unsigned long)pwr->persist_seq);
    } else {
        printf("[PWR] relay state defaults: DC=%d USB=%d FAN=%d\n",
               pwr->desired_state[PORT_DC_OUT],
               pwr->desired_state[PORT_USB_PD],
               pwr->desired_state[PORT_FAN]);
    }
}

static bool _persist_save(PowerControl *pwr) {
    RelayStatePayload payload;

    if (!pwr || !pwr->persist_ready) return false;
    payload.version = RELAY_STATE_VERSION;
    payload.mask = _persist_mask(pwr);
    payload._pad[0] = 0xFFu;
    payload._pad[1] = 0xFFu;

    if (!nvm_ab_save(FLASH_RELAY_OFFSET, FLASH_RELAY_OFFSET_B,
                     RELAY_STATE_MAGIC,
                     &pwr->persist_seq, &pwr->persist_slot,
                     &payload, sizeof(payload))) {
        printf("[PWR] relay state save FAILED\n");
        return false;
    }

    printf("[PWR] saved relay mask=0x%02X\n", payload.mask);
    return true;
}

void pwr_init(PowerControl *pwr) {
    memset(pwr, 0, sizeof(*pwr));
    memcpy(pwr->gpio, _GPIO, sizeof(_GPIO));
    pwr->policy = PWR_POLICY_ISOLATED;

    for (int i = 0; i < PORT_COUNT; i++)
        pwr->state[i] = false;

    _persist_load(pwr);
    // pseq_latch() has already asserted the hold relay before full init starts.
    pwr->state[PORT_SYSTEM_HOLD] = true;
    pwr->persist_ready = true;
    printf("[PWR] init: SYSTEM_HOLD ON, policy=ISOLATED desired DC=%d USB=%d FAN=%d\n",
           pwr->desired_state[PORT_DC_OUT],
           pwr->desired_state[PORT_USB_PD],
           pwr->desired_state[PORT_FAN]);
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
        pwr_restore_user_state(pwr);
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
        pwr->state[p] = false;
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

bool pwr_user_desired_on(const PowerControl *pwr, PortId p) {
    if (!pwr || !_port_is_user_persisted(p)) return false;
    return pwr->desired_state[p];
}

void pwr_restore_user_state(PowerControl *pwr) {
    if (!pwr) return;
    pwr_enable(pwr, PORT_SYSTEM_HOLD);
    if (pwr->desired_state[PORT_DC_OUT]) pwr_enable(pwr, PORT_DC_OUT);
    else                                 pwr_disable(pwr, PORT_DC_OUT);
    if (pwr->desired_state[PORT_USB_PD]) pwr_enable(pwr, PORT_USB_PD);
    else                                 pwr_disable(pwr, PORT_USB_PD);
    if (pwr->desired_state[PORT_FAN])    pwr_enable(pwr, PORT_FAN);
    else                                 pwr_disable(pwr, PORT_FAN);
}

void pwr_user_set(PowerControl *pwr, PortId p, bool on) {
    if (!pwr || !_port_is_user_persisted(p)) return;
    pwr->desired_state[p] = on;
    if (on) pwr_enable(pwr, p);
    else    pwr_disable(pwr, p);
    _persist_save(pwr);
}

void pwr_user_toggle(PowerControl *pwr, PortId p) {
    if (!pwr || !_port_is_user_persisted(p)) return;
    pwr_user_set(pwr, p, !pwr->desired_state[p]);
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
        if (pwr->policy == PWR_POLICY_LOADS_ON) {
            pwr_restore_user_state(pwr);
        }
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

const char *pwr_name(PortId p) {
    if ((unsigned)p >= PORT_COUNT) return "UNKNOWN";
    return _NAMES[p];
}

void pwr_set_charge_inhibit(PowerControl *pwr, bool inhibit) {
    if (pwr->charge_inhibit == inhibit) return;
    pwr->charge_inhibit = inhibit;
    printf("[PWR] charge_inhibit advisory = %s (no charge relay)\n", inhibit ? "YES" : "NO");
}

bool pwr_is_charge_inhibited(const PowerControl *pwr) {
    return pwr->charge_inhibit;
}
