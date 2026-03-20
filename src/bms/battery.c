// ============================================================
// bms/battery.c — BMS Coordinator
//
// v5.0 changes:
//   FMEA-02 — per-group measurement validity mask + stale age check
//   FMEA-15 — bat_init() validates lm75a_inv; sets inv_temp_sensor_ok
// ============================================================
#include "battery.h"
#include "bms_ocv.h"
#include "../config.h"
#include "../app/power_control.h"
#include "../app/system_settings.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static inline float _clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static TempStatus _temp_status(float t, float warn, float cut) {
    if (t >= cut)  return TEMP_CRIT;
    if (t >= warn) return TEMP_WARN;
    return TEMP_OK;
}

static inline bool _finitef(float v) {
    return !isnan(v) && !isinf(v);
}

static bool _sensor_vi_sane(float v, float i) {
    return _finitef(v) && _finitef(i) &&
           v >= SENSOR_SANITY_V_MIN && v <= SENSOR_SANITY_V_MAX &&
           fabsf(i) <= SENSOR_SANITY_I_MAX_A;
}

static bool _cells_sane(float v1, float v2, float v3, float delta_mv) {
    return _finitef(v1) && _finitef(v2) && _finitef(v3) && _finitef(delta_mv) &&
           v1 >= 0.0f && v1 <= SENSOR_SANITY_CELL_V_MAX &&
           v2 >= 0.0f && v2 <= SENSOR_SANITY_CELL_V_MAX &&
           v3 >= 0.0f && v3 <= SENSOR_SANITY_CELL_V_MAX &&
           delta_mv >= 0.0f && delta_mv <= 1000.0f;
}

static bool _temp_sane(float t) {
    return _finitef(t) &&
           t >= SENSOR_SANITY_TEMP_MIN_C &&
           t <= SENSOR_SANITY_TEMP_MAX_C;
}

static inline uint32_t _ms_now(void) {
    return to_ms_since_boot(get_absolute_time());
}

// Mark a measurement group valid with current timestamp
static inline void _mark_valid(Battery *bat, int group) {
    bat->meas_valid   |= (uint8_t)(1u << group);
    bat->meas_ts_ms[group] = _ms_now();
}

// Mark a measurement group stale
static inline void _mark_stale(Battery *bat, int group) {
    bat->meas_valid &= (uint8_t)~(1u << group);
}

// FMEA-02: is a measurement group fresh?
bool bat_meas_fresh(const Battery *bat, uint8_t group_bit) {
    if (group_bit == 0) return false;
    if (!(bat->meas_valid & group_bit)) return false;
    int idx = __builtin_ctz(group_bit);
    uint32_t age = _ms_now() - bat->meas_ts_ms[idx];
    return age < MEAS_STALE_MS;
}

// ── Init ─────────────────────────────────────────────────────
void bat_init(Battery *bat,
              INA226 *ina_dis, INA226 *ina_chg,
              INA3221 *ina3221,
              LM75A *lm75a_bat, LM75A *lm75a_inv,
              PowerControl *pwr) {
    const SystemSettings *cfg = settings_get();

    memset(bat, 0, sizeof(*bat));
    bat->ina_dis   = ina_dis;
    bat->ina_chg   = ina_chg;
    bat->ina3221   = ina3221;
    bat->lm75a_bat = lm75a_bat;
    bat->lm75a_inv = lm75a_inv;
    bat->pwr       = pwr;

    // FMEA-15: note if DC-USB temp sensor is available
    bat->inv_temp_sensor_ok = (lm75a_inv != NULL);
    if (!bat->inv_temp_sensor_ok)
        printf("[BAT] WARN: no DC-USB temp sensor — fan relay blocked\n");

    bat->voltage  = cfg->pack_nominal_v;
    bat->temp_bat = 25.0f;
    bat->temp_inv = 25.0f;
    bat->v_b1 = bat->v_b2 = bat->v_b3 = cfg->pack_nominal_v / BAT_CELLS;
    bat->meas_valid = 0;  // all stale at init

    soh_init(&bat->soh_est, cfg->capacity_ah, SOH_R0_NOMINAL);
    soh_load(&bat->soh_est);
    bat_apply_settings(bat, cfg->capacity_ah);
    bat->soh = bat->soh_est.soh * 100.0f;
    bat->efc = bat->soh_est.efc;

    float soc_init = bms_ocv_pack_to_soc(bat->voltage, bat->temp_bat);
    ekf_init(&bat->ekf, soc_init);
    bat->soc = soc_init * 100.0f;
    bat->pred_confidence = 0.0f;

    bat->_t_last_logic_ms = _ms_now();
    printf("[BAT] init: V=%.3fV SOC=%.1f%% SOH=%.1f%% inv_sensor=%s\n",
           bat->voltage, bat->soc, bat->soh,
           bat->inv_temp_sensor_ok ? "OK" : "ABSENT");
}

void bat_apply_settings(Battery *bat, float capacity_ah) {
    if (!bat) return;

    bat->soh_est.q_nominal_ah = _clamp(capacity_ah, 1.0f, 120.0f);
    if (bat->soh_est.q_measured_ah < bat->soh_est.q_nominal_ah * 0.30f ||
        bat->soh_est.q_measured_ah > bat->soh_est.q_nominal_ah * 1.10f) {
        bat->soh_est.q_measured_ah = bat->soh_est.q_nominal_ah;
    }

    soh_calc(&bat->soh_est);
    bat->soh = bat->soh_est.soh * 100.0f;
    bat->efc = bat->soh_est.efc;
    bat->remaining_wh = (bat->soc / 100.0f) *
                        fmaxf(bat->soh_est.q_measured_ah, 0.1f) *
                        settings_get()->pack_nominal_v;
}

// ── Sensor read ──────────────────────────────────────────────
void bat_read_sensors(Battery *bat) {
    const SystemSettings *cfg = settings_get();
    bool ok_all = true;
    uint32_t now = _ms_now();

    // ── INA226 discharge (pack voltage + i_dis) ───────────────
    float v, i_d, p;
    if (ina226_read(bat->ina_dis, &v, &i_d, &p) && _sensor_vi_sane(v, i_d)) {
        bat->voltage = v;
        bat->i_dis   = _clamp(i_d, 0.0f, IMAX_DIS_A * 1.2f);
        _mark_valid(bat, 0);
    } else {
        printf("[BAT] ina_dis fail/sanity\n");
        bat->i_dis = 0.0f;
        _mark_stale(bat, 0);
        ok_all = false;
    }

    // ── INA226 charge (i_chg) ────────────────────────────────
    float _v, i_c, _p;
    if (ina226_read(bat->ina_chg, &_v, &i_c, &_p) && _sensor_vi_sane(_v, i_c)) {
        bat->i_chg = _clamp(fabsf(i_c), 0.0f, IMAX_CHG_A * 1.2f);
        _mark_valid(bat, 1);
    } else {
        printf("[BAT] ina_chg fail/sanity\n");
        bat->i_chg = 0.0f;
        _mark_stale(bat, 1);
        // Charge monitor is optional for normal discharge-only operation.
        // Keep charger detection disabled, but do not escalate this into a
        // full I2C fault that drops the whole system into emergency off.
    }

    bat->i_net   = bat->i_dis - bat->i_chg;
    bat->power_w = bat->voltage * fmaxf(bat->i_net, 0.0f);

    // ── INA3221 cells ─────────────────────────────────────────
    // FMEA-02: if cells stale > 600ms -> raise charge advisory + disable fan
    if (!ina3221_read_cells(bat->ina3221,
                            &bat->v_b1, &bat->v_b2, &bat->v_b3,
                            &bat->delta_mv) ||
        !_cells_sane(bat->v_b1, bat->v_b2, bat->v_b3, bat->delta_mv)) {
        printf("[BAT] ina3221 fail/sanity\n");
        bat->delta_mv = 999.0f;
        _mark_stale(bat, 2);
        ok_all = false;
        // Stale cell data: disable charge to avoid imbalance damage
        if (bat->pwr) {
            uint32_t age = now - bat->meas_ts_ms[2];
            if (age > (uint32_t)MEAS_STALE_MS) {
                pwr_set_charge_inhibit(bat->pwr, true);
                pwr_disable(bat->pwr, PORT_FAN);
                printf("[BAT] FMEA-02: cell data stale %lums -> charge advisory + fan off\n",
                       (unsigned long)age);
            }
        }
    } else {
        _mark_valid(bat, 2);
    }

    // ── LM75A battery temp ────────────────────────────────────
    float t;
    if (lm75a_read(bat->lm75a_bat, &t) && _temp_sane(t)) {
        bat->temp_bat = t;
        _mark_valid(bat, 3);
    } else {
        printf("[BAT] lm75a_bat fail/sanity\n");
        bat->temp_bat = 25.0f;
        _mark_stale(bat, 3);
        ok_all = false;
    }

    // ── LM75A inverter temp ───────────────────────────────────
    if (bat->lm75a_inv) {
        if (lm75a_read(bat->lm75a_inv, &t) && _temp_sane(t)) {
            bat->temp_inv = t;
            _mark_valid(bat, 4);
        } else {
            printf("[BAT] lm75a_inv fail/sanity\n");
            bat->temp_inv = 25.0f;
            _mark_stale(bat, 4);
            // Fan relay depends on this sensor; fail-safe disables cooling drive on sensor loss.
            if (bat->pwr && pwr_is_on(bat->pwr, PORT_FAN)) {
                pwr_disable(bat->pwr, PORT_FAN);
                printf("[BAT] FMEA-15: DC-USB temp sensor lost → fan off\n");
            }
        }
    }

    bat->temp_bat_status = _temp_status(bat->temp_bat, cfg->temp_bat_warn_c, cfg->temp_bat_safe_c);
    bat->temp_inv_status = _temp_status(bat->temp_inv, cfg->temp_inv_warn_c, cfg->temp_inv_safe_c);
    {
        bool charge_enter = (bat->i_chg > 0.30f) && (bat->i_net < -0.15f);
        bool charge_hold  = (bat->i_chg > 0.20f) && (bat->i_net < -0.05f);
        bat->is_charging = bat->is_charging ? charge_hold : charge_enter;
    }
    bat->is_idle     = fabsf(bat->i_net) < OCV_IDLE_A;

    if (ok_all) {
        bat->i2c_fail_count = 0;
    } else {
        bat->i2c_fail_count++;
        if (bat->i2c_fail_count >= I2C_MAX_FAILS) {
            printf("[BAT] I2C fail x%d — EMERGENCY OFF\n", bat->i2c_fail_count);
            bat_emergency_off(bat);
            bat->i2c_fail_count = 0;
        }
    }
}

// ── BMS logic ────────────────────────────────────────────────
void bat_update_bms(Battery *bat, float dt_s) {
    const SystemSettings *cfg = settings_get();
    bool pack_ok = bat_meas_fresh(bat, MEAS_VALID_PACK);
    bool chg_ok  = bat_meas_fresh(bat, MEAS_VALID_CHG);
    bool tbat_ok = bat_meas_fresh(bat, MEAS_VALID_TBAT);

    if (!pack_ok) {
        bat->i_net = 0.0f;
        bat->power_w = 0.0f;
        bat->time_min = 9999;
        bat->pred_confidence = 0.0f;
        return;
    }

    float temp_used = tbat_ok ? bat->temp_bat : 25.0f;
    bat->ekf.q_n = fmaxf(1.0f, bat->soh_est.q_measured_ah * 3600.0f);

    float soc_frac = ekf_step(&bat->ekf, chg_ok ? bat->i_net : bat->i_dis, bat->voltage,
                              temp_used, dt_s);
    bat->soc     = _clamp(soc_frac * 100.0f, 0.0f, 100.0f);
    bat->soc_std = bat->ekf.soc_std;
    bat->r0_mohm = bat->ekf.r0 * 1000.0f;

    if (bat->is_idle && tbat_ok) {
        if (bat->_t_idle_ms == 0)
            bat->_t_idle_ms = _ms_now();
        uint32_t idle_ms = _ms_now() - bat->_t_idle_ms;
        if (idle_ms >= (OCV_SETTLE_MS * 2u)) {
            ekf_inject_ocv(&bat->ekf, bat->voltage, temp_used, 0.35f);
            bat->_t_idle_ms = 0;
        }
    } else {
        bat->_t_idle_ms = 0;
    }

    float di = bat->i_net - bat->_i_prev;
    float dv = bat->voltage - bat->_v_prev;
    if (fabsf(di) >= 3.0f && fabsf(dv) >= 0.01f && dt_s <= 0.25f)
        ekf_update_r0(&bat->ekf, dv, di);
    bat->_v_prev = bat->voltage;
    bat->_i_prev = bat->i_net;

    if (bat->i_net > 0.1f)
        soh_update_discharge(&bat->soh_est, bat->i_net, bat->voltage, dt_s);
    else if (chg_ok && bat->i_chg > 0.1f)
        soh_update_charge(&bat->soh_est, bat->i_chg, bat->voltage, dt_s);

    bool is_now_discharging = (bat->i_net > 0.5f);
    if (!bat->_was_discharging && is_now_discharging) {
        bat->soh_est._soc_dis_start_frac = _clamp(bat->soc / 100.0f, 0.0f, 1.0f);
        bat->soh_est._ah_dis_session = 0.0f;
        bat->soh_est._wh_dis_session = 0.0f;
    }
    if (bat->_was_discharging && !is_now_discharging)
        soh_on_cycle_end(&bat->soh_est, bat->ekf.r0, bat->soc / 100.0f);
    bat->_was_discharging = is_now_discharging;

    bat->soh = bat->soh_est.soh * 100.0f;
    bat->efc = bat->soh_est.efc;
    float usable_ah = fmaxf(bat->soh_est.q_measured_ah, 0.1f);
    bat->remaining_wh = soc_frac * usable_ah * cfg->pack_nominal_v;

    if (bat->is_charging && chg_ok) {
        float net_charge_a = fmaxf(-bat->i_net, 0.0f);
        float remain_ah = fmaxf((1.0f - soc_frac) * usable_ah, 0.0f);
        bat->pred_confidence = 0.0f;
        if (net_charge_a > 0.15f && remain_ah > 0.01f) {
            float taper = (soc_frac > 0.90f) ? 1.25f : (soc_frac > 0.80f ? 1.12f : 1.05f);
            bat->time_min = (int)(remain_ah / net_charge_a * 60.0f * taper);
        } else {
            bat->time_min = 9999;
        }
    } else {
        bat->pred_confidence = 0.0f;
        bat->time_min = 9999;
    }
}

void bat_save(Battery *bat) {
    soh_calc(&bat->soh_est);
    soh_save(&bat->soh_est);
}

// ── P0.1: Real emergency off ──────────────────────────────────
void bat_emergency_off(Battery *bat) {
    printf("[BAT] EMERGENCY OFF → pwr_emergency_off()\n");
    if (bat->pwr) {
        pwr_emergency_off(bat->pwr);
        pwr_set_safe_mode(bat->pwr, true);
    }
}

// ── Snapshot ─────────────────────────────────────────────────
void bat_snapshot(const Battery *bat, BatSnapshot *out) {
    out->voltage     = bat->voltage;
    out->i_dis       = bat->i_dis;
    out->i_chg       = bat->i_chg;
    out->i_net       = bat->i_net;
    out->power_w     = bat->power_w;
    out->temp_bat    = bat->temp_bat;
    out->temp_inv    = bat->temp_inv;
    out->v_b1        = bat->v_b1;
    out->v_b2        = bat->v_b2;
    out->v_b3        = bat->v_b3;
    out->delta_mv    = bat->delta_mv;
    out->soc         = bat->soc;
    out->soc_std     = bat->soc_std;
    out->remaining_wh= bat->remaining_wh;
    out->r0_mohm     = bat->r0_mohm;
    out->soh         = bat->soh;
    out->efc         = bat->efc;
    out->is_charging = bat->is_charging;
    out->is_idle     = bat->is_idle;
    out->time_min    = bat->time_min;
    out->pred_confidence = bat->pred_confidence;
    out->temp_bat_status = (uint8_t)bat->temp_bat_status;
    out->temp_inv_status = (uint8_t)bat->temp_inv_status;
    out->dis_wh_total = bat->soh_est.dis_wh_total;
    out->chg_wh_total = bat->soh_est.chg_wh_total;
    out->dis_ah_total = bat->soh_est.dis_ah_total;
    out->chg_ah_total = bat->soh_est.chg_ah_total;
    out->meas_valid   = bat->meas_valid;
    out->inv_temp_sensor_ok = bat->inv_temp_sensor_ok;
}
