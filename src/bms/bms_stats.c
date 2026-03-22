// ============================================================
// bms/bms_stats.c — Lifetime statistics
//
// v4.0 fixes:
//   P1.6  — session_wh_out/in now correctly accumulated
//   P1.6  — runtime_h updated on each stats_save()
//   P3.14 — flash write via flash_nvm (A/B, CRC, multicore lockout)
// ============================================================
#include "bms_stats.h"
#include "flash_nvm.h"
#include "../config.h"
#include "../app/protection.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define STATS_OFF_A  FLASH_HIST_OFFSET
#define STATS_OFF_B  FLASH_HIST_OFFSET_B
#define STATS_PRED_EMA_ALPHA 0.15f
#define STATS_PEUKERT_ALPHA  0.05f
#define STATS_MIN_SESSION_WH 1.0f
#define STATS_MIN_SESSION_H  0.02f
#define STATS_DEFAULT_PEUKERT 1.04f
#define STATS_PRED_I_NOMINAL_A 3.0f

static inline float _clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline bool _finitef(float v) {
    return !isnan(v) && !isinf(v);
}

static const char *_stats_invalid_reason(const StatsFlash *f) {
    if (!f) return "null";
    if (f->version != STATS_VERSION) return "version";
    if (!_finitef(f->efc_total) || f->efc_total < 0.0f || f->efc_total > 50000.0f) return "efc";
    if (!_finitef(f->energy_in_wh) || f->energy_in_wh < 0.0f || f->energy_in_wh > 1.0e7f) return "energy_in";
    if (!_finitef(f->energy_out_wh) || f->energy_out_wh < 0.0f || f->energy_out_wh > 1.0e7f) return "energy_out";
    if (!_finitef(f->runtime_h) || f->runtime_h < 0.0f || f->runtime_h > 100000.0f) return "runtime";
    if (f->temp_samples == 0u) {
        if (!_finitef(f->temp_min_c) || !_finitef(f->temp_max_c)) return "temp_init";
    } else {
        if (!_finitef(f->temp_min_c) || !_finitef(f->temp_max_c) ||
            !_finitef(f->temp_acc_c) ||
            f->temp_min_c < (SENSOR_SANITY_TEMP_MIN_C - 10.0f) ||
            f->temp_max_c > (SENSOR_SANITY_TEMP_MAX_C + 10.0f) ||
            f->temp_min_c > f->temp_max_c) {
            return "temp_range";
        }
    }
    if (!_finitef(f->peak_current_a) || f->peak_current_a < 0.0f || f->peak_current_a > (IMAX_DIS_A * 2.0f))
        return "peak_current";
    if (!_finitef(f->peak_power_w) || f->peak_power_w < 0.0f || f->peak_power_w > 5000.0f)
        return "peak_power";
    if (!_finitef(f->soh_initial) || f->soh_initial < 0.0f || f->soh_initial > 100.0f)
        return "soh_initial";
    if (!_finitef(f->soh_last) || f->soh_last < 0.0f || f->soh_last > 100.0f)
        return "soh_last";
    if (!_finitef(f->session_energy_out_wh) || f->session_energy_out_wh < 0.0f || f->session_energy_out_wh > f->energy_out_wh)
        return "session_out";
    if (!_finitef(f->session_energy_in_wh) || f->session_energy_in_wh < 0.0f || f->session_energy_in_wh > f->energy_in_wh)
        return "session_in";
    if (!_finitef(f->session_peak_a) || f->session_peak_a < 0.0f || f->session_peak_a > fmaxf(f->peak_current_a, IMAX_DIS_A * 2.0f))
        return "session_peak";
    if (!_finitef(f->avg_discharge_wh_per_h) || f->avg_discharge_wh_per_h < 0.0f || f->avg_discharge_wh_per_h > 5000.0f)
        return "avg_power";
    if (!_finitef(f->avg_discharge_duration_h) || f->avg_discharge_duration_h < 0.0f || f->avg_discharge_duration_h > 48.0f)
        return "avg_duration";
    if (!_finitef(f->peukert_calibrated) || f->peukert_calibrated < 1.0f || f->peukert_calibrated > 1.20f)
        return "peukert";
    return NULL;
}

static void _stats_reset_payload(BmsStats *s, uint32_t boot_count, float soh_seed) {
    memset(&s->flash, 0, sizeof(s->flash));
    s->flash.version = STATS_VERSION;
    s->flash.boot_count = boot_count;
    s->flash.temp_min_c = 200.0f;
    s->flash.temp_max_c = -100.0f;
    s->flash.soh_initial = soh_seed;
    s->flash.soh_last = soh_seed;
    s->flash.peukert_calibrated = STATS_DEFAULT_PEUKERT;

    s->session_wh_out = 0.0f;
    s->session_wh_in = 0.0f;
    s->session_peak_a = 0.0f;
    s->session_peak_w = 0.0f;
    s->initialized = true;
    s->dirty = true;
}

void stats_init(BmsStats *s) {
    memset(s, 0, sizeof(*s));

    StatsFlash f;
    uint32_t seq = 0; uint8_t slot = 0;
    bool ok = nvm_ab_load(STATS_OFF_A, STATS_OFF_B,
                          STATS_MAGIC, &f, sizeof(f), &seq, &slot);

    if (ok && f.version == STATS_VERSION) {
        s->flash      = f;
        s->_nvm_seq   = seq;
        s->_nvm_slot  = slot;
        printf("[STATS] loaded: boots=%lu EFC=%.1f out=%.0fWh\n",
               (unsigned long)f.boot_count, f.efc_total, f.energy_out_wh);
    } else if (ok && f.version == 2u) {
        s->flash = f;
        s->flash.version = STATS_VERSION;
        s->flash.avg_discharge_wh_per_h = 0.0f;
        s->flash.avg_discharge_duration_h = 0.0f;
        s->flash.discharge_session_count = 0u;
        s->flash.peukert_calibrated = STATS_DEFAULT_PEUKERT;
        s->_nvm_seq = seq;
        s->_nvm_slot = slot;
        printf("[STATS] migrated v2->v3: boots=%lu EFC=%.1f out=%.0fWh\n",
               (unsigned long)f.boot_count, f.efc_total, f.energy_out_wh);
    } else {
        _stats_reset_payload(s, 0u, 100.0f);
        s->_nvm_seq  = 0;
        s->_nvm_slot = 0;
        printf("[STATS] fresh record\n");
    }

    if (!(s->flash.peukert_calibrated >= 1.0f && s->flash.peukert_calibrated <= 1.15f)) {
        s->flash.peukert_calibrated = STATS_DEFAULT_PEUKERT;
    }

    s->flash.boot_count++;
    s->initialized = true;
    s->dirty = true;
}

// P1.6: stats_update receives delta energy values, not cumulative totals
void stats_update(BmsStats *s,
                  float dis_wh_total,   // cumulative from soh_est (monotone)
                  float chg_wh_total,
                  float efc,
                  float temp_c,
                  float current_a,
                  float power_w,
                  float soh,
                  uint32_t alarm_flags_new)
{
    if (!s->initialized) return;
    StatsFlash *f = &s->flash;

    // Monotone cumulative values — just take the max
    if (dis_wh_total > f->energy_out_wh) {
        float delta_out = dis_wh_total - f->energy_out_wh;
        f->energy_out_wh  = dis_wh_total;
        // P1.6: accumulate session discharge energy
        s->session_wh_out += delta_out;
    }
    if (chg_wh_total > f->energy_in_wh) {
        float delta_in = chg_wh_total - f->energy_in_wh;
        f->energy_in_wh   = chg_wh_total;
        // P1.6: accumulate session charge energy
        s->session_wh_in  += delta_in;
    }
    if (efc > f->efc_total) f->efc_total = efc;

    // Temperature
    if (temp_c > -50.0f && temp_c < 150.0f) {
        if (temp_c < f->temp_min_c) f->temp_min_c = temp_c;
        if (temp_c > f->temp_max_c) f->temp_max_c = temp_c;
        f->temp_acc_c += temp_c;
        f->temp_samples++;
    }

    // Peaks
    if (current_a > f->peak_current_a) f->peak_current_a = current_a;
    if (power_w   > f->peak_power_w)   f->peak_power_w   = power_w;
    if (current_a > s->session_peak_a) s->session_peak_a = current_a;
    if (power_w   > s->session_peak_w) s->session_peak_w = power_w;

    // Alarm counts (edge-triggered)
    if (alarm_flags_new) {
        f->total_alarm_events++;
        if (alarm_flags_new & (ALARM_OCP_CUT | ALARM_OCP_WARN))
            f->total_ocp_events++;
        if (alarm_flags_new & (ALARM_TEMP_CUT  | ALARM_TEMP_SAFE |
                               ALARM_TEMP_BUZZ | ALARM_TEMP_WARN |
                               ALARM_INV_CUT   | ALARM_INV_SAFE |
                               ALARM_INV_WARN))
            f->total_temp_events++;
    }

    if (f->soh_initial <= 0.0f || f->soh_initial > 100.0f) f->soh_initial = soh;
    f->soh_last = soh;
    s->dirty = true;
}

bool stats_save(BmsStats *s) {
    if (!s || !s->initialized || !s->dirty) return false;

    StatsFlash *f = &s->flash;

    // P1.6: update runtime_h from actual uptime
    f->runtime_h = stats_session_hours();

    // P1.6: flush session accumulators into flash record
    f->session_energy_out_wh = s->session_wh_out;
    f->session_energy_in_wh  = s->session_wh_in;
    f->session_peak_a        = s->session_peak_a;

    const char *invalid = _stats_invalid_reason(f);
    if (invalid) {
        printf("[STATS] save skipped: invalid %s EFC=%.1f SOH=%.1f%% in=%.0fWh out=%.0fWh\n",
               invalid, f->efc_total, f->soh_last, f->energy_in_wh, f->energy_out_wh);
        return false;
    }

    bool ok = nvm_ab_save(STATS_OFF_A, STATS_OFF_B,
                          STATS_MAGIC,
                          &s->_nvm_seq, &s->_nvm_slot,
                          f, sizeof(*f));
    if (!ok) {
        printf("[STATS] save FAILED\n");
        return false;
    }
    printf("[STATS] saved: boots=%lu EFC=%.1f SOH=%.1f%%\n",
           (unsigned long)f->boot_count, f->efc_total, f->soh_last);
    s->dirty = false;
    return true;
}

void stats_reset(BmsStats *s) {
    float soh_seed;

    if (!s) return;

    soh_seed = (s->flash.soh_last > 0.0f && s->flash.soh_last <= 100.0f)
        ? s->flash.soh_last : 100.0f;
    _stats_reset_payload(s, 1u, soh_seed);
    stats_save(s);
    printf("[STATS] lifetime stats reset\n");
}

float stats_efficiency_pct(const BmsStats *s) {
    if (s->flash.energy_in_wh < 1.0f) return 0.0f;
    float e = s->flash.energy_out_wh / s->flash.energy_in_wh * 100.0f;
    return (e > 100.0f) ? 100.0f : e;
}

float stats_avg_temp_c(const BmsStats *s) {
    if (s->flash.temp_samples == 0) return 0.0f;
    return s->flash.temp_acc_c / (float)s->flash.temp_samples;
}

float stats_session_hours(void) {
    return (float)to_ms_since_boot(get_absolute_time()) / 3600000.0f;
}

void stats_record_discharge_session(BmsStats *s,
                                    float session_wh,
                                    float session_h,
                                    float session_ah,
                                    float dod_frac,
                                    float q_measured_ah,
                                    float v_nominal) {
    if (!s || !s->initialized) return;
    if (!(session_wh >= STATS_MIN_SESSION_WH) || !(session_h >= STATS_MIN_SESSION_H)) return;

    StatsFlash *f = &s->flash;
    float session_power_w = session_wh / fmaxf(session_h, 0.001f);
    if (f->discharge_session_count == 0 || f->avg_discharge_wh_per_h <= 0.0f) {
        f->avg_discharge_wh_per_h = session_power_w;
        f->avg_discharge_duration_h = session_h;
    } else {
        f->avg_discharge_wh_per_h =
            (1.0f - STATS_PRED_EMA_ALPHA) * f->avg_discharge_wh_per_h +
            STATS_PRED_EMA_ALPHA * session_power_w;
        f->avg_discharge_duration_h =
            (1.0f - STATS_PRED_EMA_ALPHA) * f->avg_discharge_duration_h +
            STATS_PRED_EMA_ALPHA * session_h;
    }
    if (f->discharge_session_count < UINT16_MAX) {
        f->discharge_session_count++;
    }

    if (dod_frac >= 0.30f && dod_frac <= 0.95f &&
        session_ah > 0.5f && q_measured_ah > 0.1f && v_nominal > 0.1f) {
        float ideal_wh = q_measured_ah * v_nominal * dod_frac;
        float avg_current_a = session_ah / fmaxf(session_h, 0.001f);
        float current_ratio = STATS_PRED_I_NOMINAL_A / fmaxf(avg_current_a, 0.05f);
        float log_base = (current_ratio > 0.0f) ? logf(current_ratio) : 0.0f;

        if (ideal_wh > 1.0f && fabsf(log_base) > 0.05f) {
            float observed_factor = _clampf(session_wh / ideal_wh, 0.70f, 1.05f);
            float n_target = 1.0f + (logf(observed_factor) / log_base);
            if (isfinite(n_target)) {
                float current = f->peukert_calibrated;
                if (!(current >= 1.0f && current <= 1.15f)) current = STATS_DEFAULT_PEUKERT;
                f->peukert_calibrated = _clampf(
                    (1.0f - STATS_PEUKERT_ALPHA) * current + STATS_PEUKERT_ALPHA * n_target,
                    1.0f, 1.15f);
            }
        }
    }

    s->dirty = true;
}

float stats_predictor_baseline_power_w(const BmsStats *s) {
    if (!s) return 0.0f;
    if (s->flash.discharge_session_count == 0) return 0.0f;
    if (!isfinite(s->flash.avg_discharge_wh_per_h)) return 0.0f;
    return (s->flash.avg_discharge_wh_per_h > 0.5f) ? s->flash.avg_discharge_wh_per_h : 0.0f;
}

float stats_predictor_peukert(const BmsStats *s) {
    if (!s) return STATS_DEFAULT_PEUKERT;
    if (!isfinite(s->flash.peukert_calibrated)) return STATS_DEFAULT_PEUKERT;
    return _clampf(s->flash.peukert_calibrated, 1.0f, 1.15f);
}
