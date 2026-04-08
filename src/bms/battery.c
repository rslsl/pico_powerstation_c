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
#include "../app/runtime_policy.h"
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

static float _eta_cutoff_soc(const Battery *bat, float current_a, float temp_c) {
    const SystemSettings *cfg = settings_get();
    float cutoff_pack_v = fmaxf(cfg->vbat_cut_v, cfg->cell_cut_v * BAT_CELLS);
    float ir_drop_v = fmaxf(bat->ekf.r0, EKF_R0_MIN) * fmaxf(current_a, 0.0f);
    float guard_v = 0.06f;
    float ocv_cut_pack_v = cutoff_pack_v + ir_drop_v + guard_v;
    return _clamp(bms_ocv_pack_to_soc(ocv_cut_pack_v, temp_c), 0.0f, 0.98f);
}

static float _eta_usable_energy_wh(Battery *bat,
                                   float soc_frac,
                                   float usable_ah,
                                   float current_a,
                                   float temp_c) {
    float soc_cut = _eta_cutoff_soc(bat, current_a, temp_c);
    float usable_soc = _clamp(soc_frac - soc_cut, 0.0f, 1.0f);
    float v_now = bms_ocv_pack(soc_frac, temp_c);
    float v_cut = bms_ocv_pack(soc_cut, temp_c);
    float avg_v = 0.5f * (v_now + v_cut);
    if (!_finitef(avg_v) || avg_v < 1.0f) {
        avg_v = settings_get()->pack_nominal_v;
    }

    bat->eta_cutoff_soc = soc_cut;
    bat->eta_usable_wh = usable_soc * usable_ah * avg_v;
    return bat->eta_usable_wh;
}

static float _display_cutoff_soc(const SystemSettings *cfg, float temp_c) {
    float cutoff_pack_v = fmaxf(cfg->vbat_cut_v, cfg->cell_cut_v * BAT_CELLS);
    return _clamp(bms_ocv_pack_to_soc(cutoff_pack_v, temp_c), 0.0f, 0.98f);
}

static float _display_full_soc(const SystemSettings *cfg,
                               float temp_c,
                               float cutoff_soc) {
    float full_soc = _clamp(bms_ocv_pack_to_soc(cfg->pack_full_v, temp_c), 0.0f, 1.0f);
    if (!_finitef(full_soc) || full_soc <= (cutoff_soc + 0.02f)) {
        full_soc = 1.0f;
    }
    return full_soc;
}

static void _update_display_metrics(Battery *bat,
                                    float soc_frac,
                                    float usable_ah,
                                    float temp_c) {
    const SystemSettings *cfg = settings_get();
    float cutoff_soc;
    float full_soc;
    float soc_span;
    float usable_soc;
    float nominal_v;

    if (!bat || !cfg) return;

    cutoff_soc = _display_cutoff_soc(cfg, temp_c);
    full_soc = _display_full_soc(cfg, temp_c, cutoff_soc);
    soc_span = full_soc - cutoff_soc;
    nominal_v = (_finitef(cfg->pack_nominal_v) && cfg->pack_nominal_v > 1.0f)
        ? cfg->pack_nominal_v
        : (BAT_CELLS * 3.7f);

    if (!_finitef(soc_span) || soc_span < 0.02f) {
        bat->display_soc_pct = _clamp(soc_frac * 100.0f, 0.0f, 100.0f);
        bat->display_available_wh = fmaxf(soc_frac, 0.0f) * usable_ah * nominal_v;
        return;
    }

    usable_soc = _clamp(soc_frac - cutoff_soc, 0.0f, soc_span);
    bat->display_soc_pct = _clamp((usable_soc / soc_span) * 100.0f, 0.0f, 100.0f);
    bat->display_available_wh = usable_soc * usable_ah * nominal_v;
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
    bat->v_chg_bus = cfg->pack_nominal_v;
    bat->temp_bat = 25.0f;
    bat->temp_inv = 25.0f;
    bat->v_b1 = bat->v_b2 = bat->v_b3 = cfg->pack_nominal_v / BAT_CELLS;
    bat->meas_valid = 0;  // all stale at init

    soh_init(&bat->soh_est, cfg->capacity_ah, BAT_R0_NOMINAL_OHM);
    soh_load(&bat->soh_est);
    bat_apply_settings(bat, cfg->capacity_ah);
    bat->soh = bat->soh_est.soh * 100.0f;
    bat->efc = bat->soh_est.efc;
    bat->soh_rul_cycles = soh_rul_cycles(&bat->soh_est);
    pred_init(&bat->pred, cfg->capacity_ah, cfg->pack_nominal_v);

    float soc_init = bms_ocv_pack_to_soc(bat->voltage, bat->temp_bat);
    ekf_init(&bat->ekf, soc_init);
    rint_init(&bat->ekf.rint, bat->soh_est.soh);
    rint_update_soc(&bat->ekf.rint, soc_init, bat->temp_bat);
    bat->ekf.r0 = rint_r0(&bat->ekf.rint, bat->temp_bat);
    bat->ekf.r1 = rint_r1(&bat->ekf.rint, bat->temp_bat);
    bat->ekf.c1 = rint_c1(&bat->ekf.rint);
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
    bat->ekf.rint.soh = _clamp(bat->soh_est.soh, 0.30f, 1.0f);
    bat->soh = bat->soh_est.soh * 100.0f;
    bat->efc = bat->soh_est.efc;
    bat->soh_rul_cycles = soh_rul_cycles(&bat->soh_est);
    pred_init(&bat->pred, bat->soh_est.q_nominal_ah, settings_get()->pack_nominal_v);
    bat->remaining_wh = (bat->soc / 100.0f) *
                        fmaxf(bat->soh_est.q_measured_ah, 0.1f) *
                        settings_get()->pack_nominal_v;
    bat->time_min = 9999;
    bat->pred_confidence = 0.0f;
    bat->_eta_discharging = false;
    bat->_eta_pause_s = PRED_RESET_IDLE_SECONDS;
    bat->eta_usable_wh = 0.0f;
    bat->eta_cutoff_soc = 0.0f;
    _update_display_metrics(bat,
                            _clamp(bat->soc / 100.0f, 0.0f, 1.0f),
                            fmaxf(bat->soh_est.q_measured_ah, 0.1f),
                            bat->temp_bat);
}

void bat_seed_predictor(Battery *bat, float baseline_power_w, float peukert_n) {
    if (!bat) return;
    pred_seed(&bat->pred, baseline_power_w, peukert_n);
}

void bat_cycle_begin(Battery *bat) {
    if (!bat) return;
    bat->soh_est._soc_dis_start_frac = _clamp(bat->soc / 100.0f, 0.0f, 1.0f);
    bat->soh_est._ah_dis_session = 0.0f;
    bat->soh_est._wh_dis_session = 0.0f;
}

void bat_cycle_end(Battery *bat) {
    float temp_used;
    float t_k;
    float t_ref_k;
    float factor;
    float r0_soh;

    if (!bat) return;

    temp_used = bat_meas_fresh(bat, MEAS_VALID_TBAT) ? bat->temp_bat : 25.0f;
    t_k = temp_used + 273.15f;
    t_ref_k = EKF_T_REF + 273.15f;
    factor = expf(EKF_KR * (1.0f / t_k - 1.0f / t_ref_k));
    r0_soh = bat->ekf.r0;
    if (_finitef(factor) && factor > 0.2f && factor < 5.0f) {
        r0_soh /= factor;
    }

    soh_on_cycle_end(&bat->soh_est, r0_soh, _clamp(bat->soc / 100.0f, 0.0f, 1.0f));
    bat->soh = bat->soh_est.soh * 100.0f;
    bat->efc = bat->soh_est.efc;
    bat->soh_rul_cycles = soh_rul_cycles(&bat->soh_est);
}

// ── Sensor read ──────────────────────────────────────────────
void bat_read_sensors(Battery *bat) {
    const SystemSettings *cfg = settings_get();
    bool ok_all = true;
    uint32_t now = _ms_now();

    // ── INA226 discharge (pack voltage + i_dis) ───────────────
    float v, i_d, p;
    if (ina226_read(bat->ina_dis, &v, &i_d, &p) && _sensor_vi_sane(v, i_d)) {
        v *= cfg->pack_dis_v_gain;
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
        _v *= cfg->pack_chg_v_gain;
        bat->v_chg_bus = _v;
        bat->i_chg = _clamp(fabsf(i_c), 0.0f, IMAX_CHG_A * 1.2f);
        _mark_valid(bat, 1);
    } else {
        printf("[BAT] ina_chg fail/sanity\n");
        bat->v_chg_bus = 0.0f;
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
    float v_b1 = 0.0f;
    float v_b2 = 0.0f;
    float v_b3 = 0.0f;
    float delta_mv = 0.0f;
    bool cells_ok = ina3221_read_cells(bat->ina3221,
                                       &v_b1, &v_b2, &v_b3,
                                       &delta_mv);
    if (cells_ok) {
        v_b1 *= cfg->cell1_v_gain;
        v_b2 *= cfg->cell2_v_gain;
        v_b3 *= cfg->cell3_v_gain;
        {
            float v_min = fminf(v_b1, fminf(v_b2, v_b3));
            float v_max = fmaxf(v_b1, fmaxf(v_b2, v_b3));
            delta_mv = (v_max - v_min) * 1000.0f;
        }
        cells_ok = _cells_sane(v_b1, v_b2, v_b3, delta_mv);
    }
    if (!cells_ok) {
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
        bat->v_b1 = v_b1;
        bat->v_b2 = v_b2;
        bat->v_b3 = v_b3;
        bat->delta_mv = delta_mv;
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
        bool charge_enter = runtime_policy_charge_enter(bat->i_chg, bat->i_net);
        bool charge_hold  = runtime_policy_charge_hold(bat->i_chg, bat->i_net);
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

    if (bat->voltage < VBAT_BROWNOUT_V) {
        pred_reset_runtime(&bat->pred);
        bat->_eta_discharging = false;
        bat->_eta_pause_s = PRED_RESET_IDLE_SECONDS;
        bat->eta_usable_wh = 0.0f;
        bat->eta_cutoff_soc = _clamp(bat->soc / 100.0f, 0.0f, 1.0f);
        bat->time_min = 0;
        bat->pred_confidence = 0.0f;
        return;
    }

    float temp_used = tbat_ok ? bat->temp_bat : 25.0f;
    bat->ekf.rint.soh = _clamp(bat->soh_est.soh, 0.30f, 1.0f);
    bat->ekf.q_n = fmaxf(1.0f, bat->soh_est.q_measured_ah * 3600.0f);

    float soc_frac = ekf_step(&bat->ekf, chg_ok ? bat->i_net : bat->i_dis, bat->voltage,
                              temp_used, dt_s);
    bat->soc     = _clamp(soc_frac * 100.0f, 0.0f, 100.0f);
    bat->soc_std = bat->ekf.soc_std;
    bat->r0_mohm = bat->ekf.r0 * 1000.0f;

    if (bat->is_idle && tbat_ok) {
        if (bat->_t_idle_ms == 0) {
            bat->_v_before_idle = bat->_v_prev;
            bat->_i_before_idle = bat->_i_prev;
            bat->_t_idle_ms = _ms_now();
        }
        uint32_t idle_ms = _ms_now() - bat->_t_idle_ms;
        if (idle_ms >= (OCV_SETTLE_MS * 2u)) {
            if (fabsf(bat->_i_before_idle) > 2.0f) {
                float dv_idle = bat->voltage - bat->_v_before_idle;
                ekf_update_r0(&bat->ekf, dv_idle, bat->_i_before_idle, temp_used);
            }
            ekf_inject_ocv(&bat->ekf, bat->voltage, temp_used, 0.35f);
            bat->_t_idle_ms = 0;
            bat->_v_before_idle = bat->voltage;
            bat->_i_before_idle = 0.0f;
        }
    } else {
        bat->_t_idle_ms = 0;
        bat->_v_before_idle = bat->voltage;
        bat->_i_before_idle = bat->i_net;
    }

    float di = bat->i_net - bat->_i_prev;
    float dv = bat->voltage - bat->_v_prev;
    if (fabsf(di) >= 1.0f && fabsf(dv) >= 0.005f && dt_s <= 0.25f)
        ekf_update_r0(&bat->ekf, dv, di, temp_used);
    bat->_v_prev = bat->voltage;
    bat->_i_prev = bat->i_net;

    if (bat->i_net > 0.1f)
        soh_update_discharge(&bat->soh_est, bat->i_net, bat->voltage, dt_s);
    else if (chg_ok && bat->i_chg > 0.1f)
        soh_update_charge(&bat->soh_est, bat->i_chg, bat->voltage, dt_s);

    bat->soh = bat->soh_est.soh * 100.0f;
    bat->efc = bat->soh_est.efc;
    bat->soh_rul_cycles = soh_rul_cycles(&bat->soh_est);
    float usable_ah = fmaxf(bat->soh_est.q_measured_ah, 0.1f);
    float display_cutoff_soc = _display_cutoff_soc(cfg, temp_used);
    float display_full_soc = _display_full_soc(cfg, temp_used, display_cutoff_soc);
    uint32_t now_ms = _ms_now();
    bat->remaining_wh = soc_frac * usable_ah * cfg->pack_nominal_v;
    _update_display_metrics(bat, soc_frac, usable_ah, temp_used);

    if (bat->is_charging && chg_ok) {
        pred_reset_runtime(&bat->pred);
        bat->_eta_discharging = false;
        bat->_eta_pause_s = PRED_RESET_IDLE_SECONDS;
        bat->eta_usable_wh = 0.0f;
        bat->eta_cutoff_soc = _clamp(soc_frac, 0.0f, 1.0f);
        float net_charge_a = fmaxf(-bat->i_net, 0.0f);
        float remain_ah = fmaxf((display_full_soc - soc_frac) * usable_ah, 0.0f);
        bat->pred_confidence = 0.0f;
        if (net_charge_a > 0.15f && remain_ah > 0.01f) {
            float taper = (soc_frac > 0.90f) ? 1.25f : (soc_frac > 0.80f ? 1.12f : 1.05f);
            bat->time_min = (int)(remain_ah / net_charge_a * 60.0f * taper);
        } else {
            bat->time_min = 9999;
        }
    } else {
        bool discharge_enter = runtime_policy_discharge_enter(bat->is_charging, bat->i_net, bat->power_w);
        bool discharge_hold  = runtime_policy_discharge_hold(bat->is_charging, bat->i_net, bat->power_w);
        bool discharge_eta_active = bat->_eta_discharging ? discharge_hold : discharge_enter;

        if (discharge_eta_active) {
            if (!bat->_eta_discharging && bat->_eta_pause_s >= PRED_RESET_IDLE_SECONDS) {
                pred_reset_runtime(&bat->pred);
            }
            bat->_eta_discharging = true;
            bat->_eta_pause_s = 0.0f;

            float usable_energy_wh = _eta_usable_energy_wh(bat,
                                                           soc_frac,
                                                           usable_ah,
                                                           fmaxf(bat->i_net, 0.0f),
                                                           temp_used);
            int pred_min = pred_update(&bat->pred,
                                       soc_frac,
                                       bat->i_net,
                                       bat->power_w,
                                       usable_energy_wh,
                                       temp_used,
                                       bat->soh_est.soh,
                                       bat->ekf.r0,
                                       dt_s);
            bat->pred_confidence = bat->pred.confidence;
            bat->time_min = (pred_min > 0 && pred_min < 9999) ? pred_min : 9999;

            if ((now_ms - bat->_eta_log_ms) >= 10000u) {
                bat->_eta_log_ms = now_ms;
                printf("[ETA] P=%.1fW fast=%.1f slow=%.1f win=%.1f cons=%.1f usable=%.1fWh cutSOC=%.1f%% raw=%dm disp=%dm conf=%.2f R0=%.1fmOhm\n",
                       bat->power_w,
                       bat->pred.power_fast_w,
                       bat->pred.power_slow_w,
                       bat->pred.power_window_w,
                       bat->pred.power_cons_w,
                       bat->eta_usable_wh,
                       bat->eta_cutoff_soc * 100.0f,
                       bat->pred.minutes_raw,
                       bat->pred.minutes_display,
                       bat->pred.confidence,
                       bat->ekf.r0 * 1000.0f);
            }
        } else {
            bat->_eta_discharging = false;
            bat->_eta_pause_s += dt_s;
            bat->eta_usable_wh = 0.0f;
            bat->eta_cutoff_soc = _clamp(soc_frac, 0.0f, 1.0f);
            if (bat->_eta_pause_s >= PRED_RESET_IDLE_SECONDS) {
                pred_reset_runtime(&bat->pred);
            }
            bat->pred_confidence = 0.0f;
            bat->time_min = 9999;
        }
    }
}

bool bat_save(Battery *bat) {
    if (!bat) return false;
    soh_calc(&bat->soh_est);
    bat->ekf.rint.soh = _clamp(bat->soh_est.soh, 0.30f, 1.0f);
    return soh_save(&bat->soh_est);
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
    out->v_chg_bus   = bat->v_chg_bus;
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
    out->display_soc_pct = bat->display_soc_pct;
    out->display_available_wh = bat->display_available_wh;
    out->r0_mohm     = bat->r0_mohm;
    out->soh         = bat->soh;
    out->efc         = bat->efc;
    out->is_charging = bat->is_charging;
    out->is_idle     = bat->is_idle;
    out->time_min    = bat->time_min;
    out->soh_rul_cycles = bat->soh_rul_cycles;
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
