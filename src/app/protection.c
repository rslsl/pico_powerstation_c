// ============================================================
// app/protection.c — BMS Protection Logic
//
// v4.0 fixes (P2.9):
//   - Debounce: N consecutive samples before alarm activates
//   - Hysteresis: separate set/clear thresholds, M samples to clear
//   - Latched faults: TEMP_CUT, I2C_FAULT — held until prot_reset_latch()
//     + prot_reset_latch() requires condition to actually be gone
// ============================================================
#include "protection.h"
#include "config.h"
#include "system_settings.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

void prot_init(Protection *prot, PowerControl *pwr) {
    memset(prot, 0, sizeof(*prot));
    prot->pwr = pwr;
}

static inline bool _fresh(const Battery *bat, uint8_t bit) {
    return bat_meas_fresh(bat, bit);
}

uint32_t prot_resettable_latch_bits(const Protection *prot, uint32_t alarm_bits) {
    uint32_t resettable = 0;
    for (int bit = 0; bit < PROT_NUM_ALARMS; ++bit) {
        uint32_t mask = 1u << bit;
        if (!(alarm_bits & mask)) continue;
        if (!(prot->latched & mask)) continue;
        if (prot->clear_count[bit] >= PROT_CLEAR_DEBOUNCE) resettable |= mask;
    }
    return resettable;
}


// ── Debounce helper ───────────────────────────────────────────
// bit:      alarm bit position (0..14)
// cond_set: true if value is ABOVE the SET threshold (alarm should trigger)
// cond_clr: true if value is BELOW the CLEAR threshold (alarm should deactivate)
// debounce_set: required consecutive samples to set
// debounce_clr: required consecutive samples to clear
static void _debounce(Protection *prot, int bit,
                      bool cond_set, bool cond_clr,
                      int debounce_set, int debounce_clr) {
    uint32_t mask = 1u << bit;
    bool currently_active = (prot->alarms & mask) != 0;

    if (cond_set) {
        prot->set_count[bit]++;
        if (prot->set_count[bit] > 250) prot->set_count[bit] = 250;
        prot->clear_count[bit] = 0;
        if (!currently_active && prot->set_count[bit] >= (uint8_t)debounce_set) {
            prot->alarms |= mask;
        }
    } else if (cond_clr && currently_active) {
        prot->clear_count[bit]++;
        if (prot->clear_count[bit] > 250) prot->clear_count[bit] = 250;
        prot->set_count[bit] = 0;
        if (prot->clear_count[bit] >= (uint8_t)debounce_clr) {
            // Don't clear if latched
            if (!(prot->latched & mask))
                prot->alarms &= ~mask;
        }
    } else {
        // Between thresholds (hysteresis band): no change to alarm, reset set counter
        if (!currently_active) prot->set_count[bit] = 0;
    }
}

// ── Main protection check ─────────────────────────────────────
void prot_check(Protection *prot, const Battery *bat) {
    const SystemSettings *cfg = settings_get();
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    prot->alarms_prev = prot->alarms;
    if (prot->ocp_cut_strikes > 0u &&
        (now_ms - prot->ocp_last_cut_ms) > 60000u) {
        prot->ocp_cut_strikes = 0u;
    }

    // ── Debounced alarm evaluation ────────────────────────────

    bool pack_ok  = _fresh(bat, MEAS_VALID_PACK);
    bool chg_ok   = _fresh(bat, MEAS_VALID_CHG);
    bool cells_ok = _fresh(bat, MEAS_VALID_CELLS);
    bool tbat_ok  = _fresh(bat, MEAS_VALID_TBAT);
    bool tinv_ok  = _fresh(bat, MEAS_VALID_TINV) && bat->inv_temp_sensor_ok;

    if (!cells_ok || !tbat_ok || !pack_ok)
        pwr_set_charge_inhibit(prot->pwr, true);

    // SOC WARN
    _debounce(prot, 0,
              pack_ok && (bat->soc <= SOC_WARN_PCT),
              pack_ok && (bat->soc >= (SOC_WARN_PCT + SOC_WARN_CLR_PCT_DELTA)),
              PROT_DEBOUNCE_WARN, PROT_CLEAR_DEBOUNCE);
    // SOC CUT (also latched)
    _debounce(prot, 1,
              pack_ok && (bat->soc <= SOC_CUTOFF_PCT),
              pack_ok && (bat->soc >= (SOC_CUTOFF_PCT + SOC_CUT_CLR_PCT_DELTA)),
              PROT_DEBOUNCE_CUT, PROT_CLEAR_DEBOUNCE);

    // VBAT WARN
    _debounce(prot, 2,
              pack_ok && (bat->voltage <= cfg->vbat_warn_v),
              pack_ok && (bat->voltage >= (cfg->vbat_warn_v + VBAT_WARN_CLR_MARGIN_V)),
              PROT_DEBOUNCE_WARN, PROT_CLEAR_DEBOUNCE);
    // VBAT CUT
    _debounce(prot, 3,
              pack_ok && (bat->voltage <= cfg->vbat_cut_v),
              pack_ok && (bat->voltage >= (cfg->vbat_cut_v + VBAT_CUT_CLR_MARGIN_V)),
              PROT_DEBOUNCE_CUT, PROT_CLEAR_DEBOUNCE);

    // Cell WARN
    float v_min = bat->v_b1;
    float v_max = bat->v_b1;
    if (bat->v_b2 < v_min) v_min = bat->v_b2;
    if (bat->v_b3 < v_min) v_min = bat->v_b3;
    if (bat->v_b2 > v_max) v_max = bat->v_b2;
    if (bat->v_b3 > v_max) v_max = bat->v_b3;
    _debounce(prot, 4,
              cells_ok && (v_min <= cfg->cell_warn_v),
              cells_ok && (v_min >= (cfg->cell_warn_v + CELL_WARN_CLR_MARGIN_V)),
              PROT_DEBOUNCE_WARN, PROT_CLEAR_DEBOUNCE);
    // Cell CUT
    _debounce(prot, 5,
              cells_ok && (v_min <= cfg->cell_cut_v),
              cells_ok && (v_min >= (cfg->cell_cut_v + CELL_CUT_CLR_MARGIN_V)),
              PROT_DEBOUNCE_CUT, PROT_CLEAR_DEBOUNCE);

    // Delta WARN
    _debounce(prot, 6,
              cells_ok && (bat->delta_mv >= DELTA_WARN_MV),
              cells_ok && (bat->delta_mv <= (DELTA_WARN_MV - DELTA_WARN_CLR_MARGIN_MV)),
              PROT_DEBOUNCE_WARN, PROT_CLEAR_DEBOUNCE);
    // Delta CUT
    _debounce(prot, 7,
              cells_ok && (bat->delta_mv >= DELTA_CUT_MV),
              cells_ok && (bat->delta_mv <= (DELTA_CUT_MV - DELTA_CUT_CLR_MARGIN_MV)),
              PROT_DEBOUNCE_CUT, PROT_CLEAR_DEBOUNCE);

    // OCP WARN
    _debounce(prot, 8,
              pack_ok && (bat->i_dis >= IDIS_WARN_A),
              pack_ok && (bat->i_dis <= (IDIS_WARN_A - IDIS_WARN_CLR_MARGIN_A)),
              PROT_DEBOUNCE_WARN, PROT_CLEAR_DEBOUNCE);
    // OCP CUT — fast (OCP is instantaneous event, 1 sample enough for cut)
    _debounce(prot, 9,
              pack_ok && (bat->i_dis >= IDIS_CUT_A),
              pack_ok && (bat->i_dis <= (IDIS_CUT_A - IDIS_CUT_CLR_MARGIN_A)),
              1, PROT_CLEAR_DEBOUNCE);

    // TEMP BAT WARN
    _debounce(prot, 10,
              tbat_ok && (bat->temp_bat >= cfg->temp_bat_warn_c),
              tbat_ok && (bat->temp_bat <= (cfg->temp_bat_warn_c - TEMP_BAT_WARN_CLR_MARGIN_C)),
              PROT_DEBOUNCE_WARN, PROT_CLEAR_DEBOUNCE);
    // TEMP BAT BUZZ
    _debounce(prot, 17,
              tbat_ok && (bat->temp_bat >= cfg->temp_bat_buzz_c),
              tbat_ok && (bat->temp_bat <= (cfg->temp_bat_buzz_c - TEMP_BAT_BUZZ_CLR_MARGIN_C)),
              PROT_DEBOUNCE_WARN, PROT_CLEAR_DEBOUNCE);
    // TEMP BAT SAFE
    _debounce(prot, 18,
              tbat_ok && (bat->temp_bat >= cfg->temp_bat_safe_c),
              tbat_ok && (bat->temp_bat <= (cfg->temp_bat_safe_c - TEMP_BAT_SAFE_CLR_MARGIN_C)),
              PROT_DEBOUNCE_CUT, PROT_CLEAR_DEBOUNCE);
    // TEMP BAT CUT — latched emergency
    _debounce(prot, 11,
              tbat_ok && (bat->temp_bat >= cfg->temp_bat_cut_c),
              tbat_ok && (bat->temp_bat <= (cfg->temp_bat_cut_c - TEMP_BAT_CUT_CLR_MARGIN_C)),
              PROT_DEBOUNCE_CUT, PROT_CLEAR_DEBOUNCE);

    // TEMP INV WARN
    _debounce(prot, 12,
              tinv_ok && (bat->temp_inv >= cfg->temp_inv_warn_c),
              tinv_ok && (bat->temp_inv <= (cfg->temp_inv_warn_c - TEMP_INV_WARN_CLR_MARGIN_C)),
              PROT_DEBOUNCE_WARN, PROT_CLEAR_DEBOUNCE);
    // TEMP INV SAFE
    _debounce(prot, 19,
              tinv_ok && (bat->temp_inv >= cfg->temp_inv_safe_c),
              tinv_ok && (bat->temp_inv <= (cfg->temp_inv_safe_c - TEMP_INV_SAFE_CLR_MARGIN_C)),
              PROT_DEBOUNCE_CUT, PROT_CLEAR_DEBOUNCE);
    // TEMP INV CUT
    _debounce(prot, 13,
              tinv_ok && (bat->temp_inv >= cfg->temp_inv_cut_c),
              tinv_ok && (bat->temp_inv <= (cfg->temp_inv_cut_c - TEMP_INV_CUT_CLR_MARGIN_C)),
              PROT_DEBOUNCE_CUT, PROT_CLEAR_DEBOUNCE);

    // Cell OVP -> charge protection
    _debounce(prot, 15,
              cells_ok && (v_max >= BAT_CELL_OVP_V),
              cells_ok && (v_max <= CELL_OVP_CLR_V),
              1, PROT_CLEAR_DEBOUNCE);

    // Cold-charge protection -> inhibit charging below 0C
    _debounce(prot, 16,
              tbat_ok && (bat->temp_bat <= cfg->temp_bat_charge_min_c),
              tbat_ok && (bat->temp_bat >= (cfg->temp_bat_charge_min_c + TEMP_BAT_CHARGE_CLR_MARGIN_C)),
              PROT_DEBOUNCE_WARN, PROT_CLEAR_DEBOUNCE);

    bool i2c_stale = (!cells_ok || !pack_ok || !tbat_ok);
    _debounce(prot, 14,
              i2c_stale, !i2c_stale,
              PROT_DEBOUNCE_CUT, PROT_CLEAR_DEBOUNCE);

    // ── Latch critical faults ─────────────────────────────────
    // Once any of these triggers, it stays in latched even if alarm clears.
    uint32_t new_alarms = prot->alarms & ~prot->alarms_prev;
    uint32_t latch_mask = ALARM_TEMP_CUT | ALARM_TEMP_SAFE |
                          ALARM_INV_CUT  | ALARM_INV_SAFE |
                          ALARM_I2C_FAULT;
    if (new_alarms & latch_mask) {
        prot->latched |= (new_alarms & latch_mask);
        prot->alarms  |= prot->latched;   // keep latched bits in active alarms
    }
    // Keep latched bits always set in alarms
    prot->alarms |= prot->latched;

    // Recompute new_alarms after latching
    new_alarms = prot->alarms & ~prot->alarms_prev;
    if (new_alarms) prot->alarm_count++;

    if ((prot->latched & ALARM_I2C_FAULT) &&
        !i2c_stale &&
        prot->clear_count[14] >= PROT_I2C_RECOVERY_SAMPLES) {
        prot->latched &= ~ALARM_I2C_FAULT;
        prot->alarms &= ~ALARM_I2C_FAULT;
        prot->set_count[14] = 0;
        prot->clear_count[14] = 0;
        printf("[PROT] I2C fault auto-recovered after 5s stable sensors\n");
    }

    if (prot->ocp_cut_holdoff &&
        (!pack_ok || bat->i_dis <= (IDIS_CUT_A - IDIS_CUT_CLR_MARGIN_A))) {
        prot->ocp_cut_holdoff = false;
    }

    if (new_alarms & ALARM_OCP_CUT) {
        bool second_strike = (prot->ocp_cut_strikes > 0u) &&
                             ((now_ms - prot->ocp_last_cut_ms) <= 60000u) &&
                             !prot->ocp_cut_holdoff;
        prot->ocp_last_cut_ms = now_ms;
        if (!second_strike) {
            prot->ocp_cut_strikes = 1u;
            prot->ocp_cut_holdoff = true;
            prot->alarms &= ~ALARM_OCP_CUT;
            prot->alarms |= ALARM_OCP_WARN;
            prot->set_count[9] = 0;
            prot->clear_count[9] = 0;
            new_alarms &= ~ALARM_OCP_CUT;
            if (!(prot->alarms_prev & ALARM_OCP_WARN)) {
                new_alarms |= ALARM_OCP_WARN;
            }
            printf("[PROT] OCP strike 1/2: %.1fA warning only\n", bat->i_dis);
        } else {
            prot->ocp_cut_strikes = 2u;
            prot->ocp_cut_holdoff = false;
        }
    }

    bool charge_block = (!cells_ok || !pack_ok || !tbat_ok || !chg_ok ||
                         (prot->alarms & (ALARM_DELTA_CUT | ALARM_TEMP_CUT |
                                          ALARM_I2C_FAULT | ALARM_CELL_CUT |
                                          ALARM_CELL_OVP | ALARM_COLD_CHARGE)));
    pwr_set_charge_inhibit(prot->pwr, charge_block);

    // ── Hardware actions ──────────────────────────────────────

    // Discharge cutoff → disable all discharge outputs including DC_OUT
    if (prot->alarms & (ALARM_SOC_CUT | ALARM_VBAT_CUT | ALARM_CELL_CUT)) {
        pwr_disable(prot->pwr, PORT_DC_OUT);
        pwr_disable(prot->pwr, PORT_USB_PD);
        pwr_disable(prot->pwr, PORT_FAN);
        if (new_alarms & (ALARM_SOC_CUT | ALARM_VBAT_CUT | ALARM_CELL_CUT))
            printf("[PROT] CUTOFF: SOC=%.1f%% V=%.3fV\n",
                   bat->soc, bat->voltage);
    }

    // FMEA-04: OCP → cut ALL discharge-class outputs including DC_OUT
    if (prot->alarms & ALARM_OCP_CUT) {
        pwr_disable(prot->pwr, PORT_DC_OUT);
        pwr_disable(prot->pwr, PORT_USB_PD);
        pwr_disable(prot->pwr, PORT_FAN);
        if (new_alarms & ALARM_OCP_CUT)
            printf("[PROT] OCP: %.1fA — DC/USB-PD/FAN off\n", bat->i_dis);
    }

    uint32_t thermal_safe_mask = ALARM_TEMP_SAFE | ALARM_INV_SAFE;
    bool thermal_safe_active = (prot->alarms & thermal_safe_mask) != 0;
    bool thermal_safe_latched = (prot->latched & thermal_safe_mask) != 0;
    if (thermal_safe_active || thermal_safe_latched) {
        pwr_set_safe_mode(prot->pwr, true);
        if (new_alarms & ALARM_TEMP_SAFE)
            printf("[PROT] BAT TEMP SAFE: %.1f°C (latched)\n", bat->temp_bat);
        if (new_alarms & ALARM_INV_SAFE)
            printf("[PROT] INV TEMP SAFE: %.1f°C (latched)\n", bat->temp_inv);
    } else if (pwr_policy_get(prot->pwr) != PWR_POLICY_ISOLATED &&
               pwr_policy_get(prot->pwr) != PWR_POLICY_FAULT_LATCH) {
        pwr_set_safe_mode(prot->pwr, false);
    }

    // Bat overtemp → FULL emergency (latched) — includes CHARGE (P0.1/P2.9)
    if (prot->alarms & ALARM_TEMP_CUT) {
        pwr_emergency_off(prot->pwr);
        if (new_alarms & ALARM_TEMP_CUT)
            printf("[PROT] TEMP CUT: %.1f°C (latched)\n", bat->temp_bat);
    }

    // Inv overtemp → FULL emergency
    if (prot->alarms & ALARM_INV_CUT) {
        pwr_emergency_off(prot->pwr);
        if (new_alarms & ALARM_INV_CUT)
            printf("[PROT] INV TEMP CUT: %.1f°C (latched)\n", bat->temp_inv);
    }

    // High delta -> raise charge advisory (no charge relay in this HW)
    if (prot->alarms & ALARM_DELTA_CUT) {
        pwr_set_charge_inhibit(prot->pwr, true);
        if (new_alarms & ALARM_DELTA_CUT)
            printf("[PROT] DELTA CUT advisory: %.0fmV\n", bat->delta_mv);
    }

    if (prot->alarms & ALARM_CELL_OVP) {
        pwr_set_charge_inhibit(prot->pwr, true);
        if (new_alarms & ALARM_CELL_OVP)
            printf("[PROT] CELL OVP advisory: vmax=%.3fV\n", v_max);
    }

    if (prot->alarms & ALARM_COLD_CHARGE) {
        pwr_set_charge_inhibit(prot->pwr, true);
        if (new_alarms & ALARM_COLD_CHARGE)
            printf("[PROT] COLD CHARGE inhibit: %.1fC\n", bat->temp_bat);
    }
}

// ── Latch reset ───────────────────────────────────────────────
// Only clears latch if condition is gone (alarms bit already cleared by debounce)
void prot_reset_latch(Protection *prot, uint32_t bits) {
    uint32_t safe_to_clear = prot_resettable_latch_bits(prot, bits);
    if (safe_to_clear) {
        prot->latched &= ~safe_to_clear;
        prot->alarms  &= ~safe_to_clear;
        printf("[PROT] latch reset: 0x%04lX\n",
               (unsigned long)safe_to_clear);
    }
}

bool prot_has_critical(const Protection *prot) {
    return (prot->alarms & ALARM_ANY_CRIT) != 0;
}
bool prot_has_warning(const Protection *prot) {
    return prot->alarms != 0 && !prot_has_critical(prot);
}
uint32_t prot_alarms(const Protection *prot) {
    return prot->alarms;
}

void prot_set_i2c_fault(Protection *prot, bool active) {
    if (active) {
        prot->alarms |= ALARM_I2C_FAULT;
        prot->latched |= ALARM_I2C_FAULT;
        prot->set_count[14] = PROT_DEBOUNCE_CUT;
        prot->clear_count[14] = 0;
    }
}
