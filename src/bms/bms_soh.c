// ============================================================
// bms/bms_soh.c — SOH Estimator
//
// v4.0 fixes:
//   P0.2  — _flash_write() replaced by nvm_ab_save() (multicore lockout)
//   P1.5  — Wh = V*I*dt/3600; separate dis_wh/dis_ah/chg_wh/chg_ah
//   P1.4  — EFC incremented continuously in soh_update_discharge()
//            soh_on_cycle_end() does q_measured_ah EMA + R0 ref update
//   P2.11 — CRC + A/B slots via flash_nvm
// ============================================================
#include "bms_soh.h"
#include "flash_nvm.h"
#include "bms_ekf.h"
#include "../config.h"
#include "pico/stdlib.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// A/B slot offsets: two consecutive sectors for SOH
#define SOH_OFF_A   FLASH_SOH_OFFSET
#define SOH_OFF_B   FLASH_SOH_OFFSET_B
// (config.h already reserves sector -1 and -2; -2 is FLASH_HIST_OFFSET;
//  we re-use -1 as slot A and claim -2 as slot B for SOH.
//  FLASH_HIST_OFFSET used by bms_stats — they are different offsets.)

static inline float _clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ── Init ─────────────────────────────────────────────────────
void soh_init(BmsSoh *s, float q_nominal_ah, float r0_nominal_ohm) {
    memset(s, 0, sizeof(*s));
    s->q_nominal_ah   = q_nominal_ah;
    s->q_measured_ah  = q_nominal_ah;
    s->r0_nominal_ohm = r0_nominal_ohm;
    s->r0_ref_ohm     = r0_nominal_ohm;
    s->soh   = 1.0f;
    s->soh_q = 1.0f;
    s->soh_r = 1.0f;
    s->_soc_dis_start_frac = 1.0f;
    s->_nvm_slot = 0;
    s->_nvm_seq  = 0;
}

// ── Load ─────────────────────────────────────────────────────
void soh_load(BmsSoh *s) {
    SohFlash f;
    uint32_t seq = 0;
    uint8_t  slot = 0;

    bool ok = nvm_ab_load(SOH_OFF_A, SOH_OFF_B,
                          SOH_MAGIC, &f, sizeof(f),
                          &seq, &slot);
    if (!ok || f.version != SOH_VERSION) {
        printf("[SOH] no valid data, defaults\n");
        return;
    }

    s->efc           = (f.efc > 0.0f) ? f.efc : 0.0f;
    s->cycle_count   = f.cycle_count;
    float q = f.q_measured_ah;
    s->q_measured_ah = (q > 0.1f && q < s->q_nominal_ah * 1.5f)
                       ? q : s->q_nominal_ah;
    float r = f.r0_ref_ohm;
    s->r0_ref_ohm    = (r > 0.001f && r < 0.5f) ? r : s->r0_nominal_ohm;
    s->soh   = _clampf(f.soh,   0.01f, 1.0f);
    s->soh_q = _clampf(f.soh_q, 0.01f, 1.0f);
    s->soh_r = _clampf(f.soh_r, 0.01f, 1.0f);

    s->dis_wh_total  = (f.dis_wh_total > 0.0f) ? f.dis_wh_total : 0.0f;
    s->dis_ah_total  = (f.dis_ah_total > 0.0f) ? f.dis_ah_total : 0.0f;
    s->chg_wh_total  = (f.chg_wh_total > 0.0f) ? f.chg_wh_total : 0.0f;
    s->chg_ah_total  = (f.chg_ah_total > 0.0f) ? f.chg_ah_total : 0.0f;
    s->wh_total      = s->dis_wh_total;  // backward compat alias

    s->_nvm_seq  = seq;
    s->_nvm_slot = slot;

    printf("[SOH] loaded: efc=%.1f soh=%.1f%% q=%.2fAh disWh=%.0f\n",
           s->efc, s->soh * 100.0f, s->q_measured_ah, s->dis_wh_total);
}

// ── Save (via flash_nvm A/B) ──────────────────────────────────
void soh_save(const BmsSoh *s) {
    SohFlash f;
    memset(&f, 0, sizeof(f));
    f.version        = SOH_VERSION;
    f.efc            = s->efc;
    f.cycle_count    = s->cycle_count;
    f.q_measured_ah  = s->q_measured_ah;
    f.r0_ref_ohm     = s->r0_ref_ohm;
    f.soh            = s->soh;
    f.soh_q          = s->soh_q;
    f.soh_r          = s->soh_r;
    f.dis_wh_total   = s->dis_wh_total;
    f.dis_ah_total   = s->dis_ah_total;
    f.chg_wh_total   = s->chg_wh_total;
    f.chg_ah_total   = s->chg_ah_total;

    // cast away const for the seq/slot — safe, they are per-call state
    BmsSoh *sm = (BmsSoh *)(uintptr_t)s;
    bool ok = nvm_ab_save(SOH_OFF_A, SOH_OFF_B,
                          SOH_MAGIC,
                          &sm->_nvm_seq, &sm->_nvm_slot,
                          &f, sizeof(f));
    if (!ok)
        printf("[SOH] save FAILED\n");
    else
        printf("[SOH] saved: efc=%.1f soh=%.1f%% slot=%d seq=%lu\n",
               s->efc, s->soh * 100.0f,
               sm->_nvm_slot, (unsigned long)sm->_nvm_seq);
}

// ── Online discharge update ───────────────────────────────────
// P1.5: correct Wh = V * I * dt / 3600
// P1.4: EFC accumulated incrementally every tick (not on cycle end)
void soh_update_discharge(BmsSoh *s, float i_a, float v, float dt_s) {
    if (i_a < 0.1f) return;
    float dah = i_a * dt_s / 3600.0f;
    float dwh = v * i_a * dt_s / 3600.0f;   // P1.5 fix: multiply by V

    s->_ah_dis_session += dah;
    s->_wh_dis_session += dwh;
    s->dis_ah_total    += dah;
    s->dis_wh_total    += dwh;
    s->wh_total         = s->dis_wh_total;  // keep alias in sync

    // P1.4: incremental EFC — no longer waiting for cycle end
    s->efc += dah / s->q_nominal_ah;
}

// ── Online charge update ──────────────────────────────────────
void soh_update_charge(BmsSoh *s, float i_a, float v, float dt_s) {
    if (i_a < 0.1f) return;
    float dah = i_a * dt_s / 3600.0f;
    float dwh = v * i_a * dt_s / 3600.0f;
    s->_ah_chg_session += dah;
    s->_wh_chg_session += dwh;
    s->chg_ah_total    += dah;
    s->chg_wh_total    += dwh;
}

// ── Cycle end (P1.4) ──────────────────────────────────────────
// Called by battery.c when discharge → idle/charge transition detected.
// Updates q_measured_ah EMA and R0 reference.
// NOTE: EFC is already accumulated incrementally above — do NOT add delta_efc here.
void soh_on_cycle_end(BmsSoh *s, float r0_current, float soc_end_frac) {
    // Estimate full capacity only when partial cycle has enough depth and final SOC anchor is meaningful.
    float soc_start_frac = _clampf(s->_soc_dis_start_frac, 0.0f, 1.0f);
    float dod_frac = soc_start_frac - soc_end_frac;
    if (dod_frac >= 0.30f && dod_frac <= 0.95f && s->_ah_dis_session > 0.5f) {
        float q_meas = s->_ah_dis_session / dod_frac;
        q_meas = _clampf(q_meas, 0.3f * s->q_nominal_ah, 1.1f * s->q_nominal_ah);
        s->q_measured_ah = 0.9f * s->q_measured_ah + 0.1f * q_meas;
        s->cycle_count++;
        printf("[SOH] cycle end: DoD=%.0f%% q_meas=%.2fAh EFC=%.1f\n",
               dod_frac * 100.0f, q_meas, s->efc);
    }
    // Update R0 reference
    if (r0_current > SOH_R0_MIN && r0_current < SOH_R0_MAX) {
        s->r0_ref_ohm = 0.95f * s->r0_ref_ohm + 0.05f * r0_current;
    }
    // Reset session accumulators
    s->_ah_dis_session = 0.0f;
    s->_wh_dis_session = 0.0f;
    s->_ah_chg_session = 0.0f;
    s->_wh_chg_session = 0.0f;
    s->_soc_dis_start_frac = _clampf(soc_end_frac, 0.0f, 1.0f);

    soh_calc(s);
}

// ── SOH calculation ───────────────────────────────────────────
void soh_calc(BmsSoh *s) {
    s->soh_q = _clampf(s->q_measured_ah / s->q_nominal_ah, 0.01f, 1.0f);
    float r_ratio = s->r0_nominal_ohm / fmaxf(s->r0_ref_ohm, 0.001f);
    s->soh_r = _clampf(r_ratio, 0.01f, 1.0f);
    s->soh   = fminf(s->soh_q, s->soh_r);
}

// ── RUL ──────────────────────────────────────────────────────
int soh_rul_cycles(const BmsSoh *s) {
    float life_efc = 500.0f;
    float soh_eol  = 0.80f;
    if (s->soh <= soh_eol) return 0;
    return (int)((s->soh - soh_eol) / (1.0f - soh_eol) * life_efc);
}
