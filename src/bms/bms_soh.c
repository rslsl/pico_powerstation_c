#include "bms_soh.h"
#include "flash_nvm.h"
#include "pico/stdlib.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define SOH_OFF_A FLASH_SOH_OFFSET
#define SOH_OFF_B FLASH_SOH_OFFSET_B

typedef struct __attribute__((packed)) {
    uint16_t version;
    float    efc;
    uint32_t cycle_count;
    float    q_measured_ah;
    float    r0_ref_ohm;
    float    soh;
    float    soh_q;
    float    soh_r;
    float    dis_wh_total;
    float    dis_ah_total;
    float    chg_wh_total;
    float    chg_ah_total;
    uint8_t  _pad[186];
} SohFlashV3;

static inline float _clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline bool _finitef(float v) {
    return !isnan(v) && !isinf(v);
}

static const char *_soh_invalid_reason(const BmsSoh *s) {
    if (!s) return "null";
    if (!_finitef(s->q_nominal_ah) || s->q_nominal_ah < 1.0f || s->q_nominal_ah > 200.0f)
        return "q_nominal";
    if (!_finitef(s->q_measured_ah) || s->q_measured_ah < 0.1f || s->q_measured_ah > (s->q_nominal_ah * 1.20f))
        return "q_measured";
    if (!_finitef(s->r0_nominal_ohm) || s->r0_nominal_ohm < SOH_R0_MIN || s->r0_nominal_ohm > SOH_R0_MAX)
        return "r0_nominal";
    if (!_finitef(s->r0_ref_ohm) || s->r0_ref_ohm < SOH_R0_MIN || s->r0_ref_ohm > SOH_R0_MAX)
        return "r0_ref";
    if (!_finitef(s->efc) || s->efc < 0.0f || s->efc > 50000.0f)
        return "efc";
    if (!_finitef(s->soh) || s->soh < 0.0f || s->soh > 1.0f)
        return "soh";
    if (!_finitef(s->soh_q) || s->soh_q < 0.0f || s->soh_q > 1.0f)
        return "soh_q";
    if (!_finitef(s->soh_r) || s->soh_r < 0.0f || s->soh_r > 1.0f)
        return "soh_r";
    if (!_finitef(s->dis_wh_total) || s->dis_wh_total < 0.0f || s->dis_wh_total > 1.0e7f)
        return "dis_wh";
    if (!_finitef(s->dis_ah_total) || s->dis_ah_total < 0.0f || s->dis_ah_total > 1.0e6f)
        return "dis_ah";
    if (!_finitef(s->chg_wh_total) || s->chg_wh_total < 0.0f || s->chg_wh_total > 1.0e7f)
        return "chg_wh";
    if (!_finitef(s->chg_ah_total) || s->chg_ah_total < 0.0f || s->chg_ah_total > 1.0e6f)
        return "chg_ah";
    return NULL;
}

static void _apply_loaded_common(BmsSoh *s,
                                 float efc,
                                 uint32_t cycle_count,
                                 float q_measured_ah,
                                 float dis_wh_total,
                                 float dis_ah_total,
                                 float chg_wh_total,
                                 float chg_ah_total,
                                 uint32_t seq,
                                 uint8_t slot) {
    s->efc = (efc > 0.0f) ? efc : 0.0f;
    s->cycle_count = cycle_count;

    if (q_measured_ah > 0.1f && q_measured_ah < s->q_nominal_ah * 1.5f) {
        s->q_measured_ah = q_measured_ah;
    } else {
        s->q_measured_ah = s->q_nominal_ah;
    }

    s->dis_wh_total = (dis_wh_total > 0.0f) ? dis_wh_total : 0.0f;
    s->dis_ah_total = (dis_ah_total > 0.0f) ? dis_ah_total : 0.0f;
    s->chg_wh_total = (chg_wh_total > 0.0f) ? chg_wh_total : 0.0f;
    s->chg_ah_total = (chg_ah_total > 0.0f) ? chg_ah_total : 0.0f;
    s->wh_total = s->dis_wh_total;

    s->_nvm_seq = seq;
    s->_nvm_slot = slot;
}

void soh_init(BmsSoh *s, float q_nominal_ah, float r0_nominal_ohm) {
    memset(s, 0, sizeof(*s));
    s->q_nominal_ah = q_nominal_ah;
    s->q_measured_ah = q_nominal_ah;
    s->r0_nominal_ohm = r0_nominal_ohm;
    s->r0_ref_ohm = r0_nominal_ohm;
    s->soh = 1.0f;
    s->soh_q = 1.0f;
    s->soh_r = 1.0f;
    s->soh_confidence = 0.0f;
    s->soh_q_confidence = 0.0f;
    s->soh_r_confidence = 0.0f;
    s->_soc_dis_start_frac = 1.0f;
    s->_nvm_slot = 0;
    s->_nvm_seq = 0;
    s->r0_update_count = 0u;
    s->migration_pending = false;
}

void soh_load(BmsSoh *s) {
    SohFlash f4;
    uint32_t seq = 0;
    uint8_t slot = 0;

    bool ok = nvm_ab_load(SOH_OFF_A, SOH_OFF_B,
                          SOH_MAGIC, &f4, sizeof(f4),
                          &seq, &slot);
    if (ok && f4.version == SOH_VERSION) {
        _apply_loaded_common(s,
                             f4.efc, f4.cycle_count, f4.q_measured_ah,
                             f4.dis_wh_total, f4.dis_ah_total,
                             f4.chg_wh_total, f4.chg_ah_total,
                             seq, slot);

        if (f4.r0_ref_ohm > SOH_R0_MIN && f4.r0_ref_ohm < SOH_R0_MAX) {
            s->r0_ref_ohm = f4.r0_ref_ohm;
        } else {
            s->r0_ref_ohm = s->r0_nominal_ohm;
        }
        s->r0_update_count = f4.r0_update_count;
        s->migration_pending = false;

        soh_calc(s);
        printf("[SOH] loaded: efc=%.1f soh=%.1f%% q=%.2fAh r0=%.1fmOhm r0N=%u\n",
               s->efc, s->soh * 100.0f, s->q_measured_ah,
               s->r0_ref_ohm * 1000.0f, (unsigned)s->r0_update_count);
        return;
    }

    SohFlashV3 f3;
    seq = 0;
    slot = 0;
    ok = nvm_ab_load(SOH_OFF_A, SOH_OFF_B,
                     SOH_MAGIC, &f3, sizeof(f3),
                     &seq, &slot);
    if (ok && f3.version == 3u) {
        _apply_loaded_common(s,
                             f3.efc, f3.cycle_count, f3.q_measured_ah,
                             f3.dis_wh_total, f3.dis_ah_total,
                             f3.chg_wh_total, f3.chg_ah_total,
                             seq, slot);
        s->r0_ref_ohm = s->r0_nominal_ohm;
        s->r0_update_count = 0u;
        s->migration_pending = true;

        soh_calc(s);
        printf("[SOH] migrated v3->v4, reset r0_ref to %.1fmOhm (save deferred until core1 ready)\n",
               s->r0_ref_ohm * 1000.0f);
        return;
    }

    s->migration_pending = false;
    printf("[SOH] no valid data, defaults\n");
}

bool soh_save(const BmsSoh *s) {
    const char *invalid = _soh_invalid_reason(s);
    if (invalid) {
        printf("[SOH] save skipped: invalid %s soh=%.1f%% q=%.2fAh r0=%.1fmOhm efc=%.1f\n",
               invalid,
               s ? (s->soh * 100.0f) : -1.0f,
               s ? s->q_measured_ah : -1.0f,
               s ? (s->r0_ref_ohm * 1000.0f) : -1.0f,
               s ? s->efc : -1.0f);
        return false;
    }

    SohFlash f;
    memset(&f, 0, sizeof(f));
    f.version = SOH_VERSION;
    f.efc = s->efc;
    f.cycle_count = s->cycle_count;
    f.q_measured_ah = s->q_measured_ah;
    f.r0_ref_ohm = s->r0_ref_ohm;
    f.soh = s->soh;
    f.soh_q = s->soh_q;
    f.soh_r = s->soh_r;
    f.dis_wh_total = s->dis_wh_total;
    f.dis_ah_total = s->dis_ah_total;
    f.chg_wh_total = s->chg_wh_total;
    f.chg_ah_total = s->chg_ah_total;
    f.r0_update_count = s->r0_update_count;

    BmsSoh *sm = (BmsSoh *)(uintptr_t)s;
    bool ok = nvm_ab_save(SOH_OFF_A, SOH_OFF_B,
                          SOH_MAGIC,
                          &sm->_nvm_seq, &sm->_nvm_slot,
                          &f, sizeof(f));
    if (!ok) {
        printf("[SOH] save FAILED\n");
        return false;
    } else {
        sm->migration_pending = false;
        printf("[SOH] saved: efc=%.1f soh=%.1f%% slot=%d seq=%lu\n",
               s->efc, s->soh * 100.0f,
               sm->_nvm_slot, (unsigned long)sm->_nvm_seq);
        return true;
    }
}

void soh_update_discharge(BmsSoh *s, float i_a, float v, float dt_s) {
    if (i_a < 0.1f) return;
    float dah = i_a * dt_s / 3600.0f;
    float dwh = v * i_a * dt_s / 3600.0f;

    s->_ah_dis_session += dah;
    s->_wh_dis_session += dwh;
    s->dis_ah_total += dah;
    s->dis_wh_total += dwh;
    s->wh_total = s->dis_wh_total;
    s->efc += dah / s->q_nominal_ah;
}

void soh_update_charge(BmsSoh *s, float i_a, float v, float dt_s) {
    if (i_a < 0.1f) return;
    float dah = i_a * dt_s / 3600.0f;
    float dwh = v * i_a * dt_s / 3600.0f;

    s->_ah_chg_session += dah;
    s->_wh_chg_session += dwh;
    s->chg_ah_total += dah;
    s->chg_wh_total += dwh;
}

void soh_on_cycle_end(BmsSoh *s, float r0_current, float soc_end_frac) {
    float soc_start_frac = _clampf(s->_soc_dis_start_frac, 0.0f, 1.0f);
    float dod_frac = soc_start_frac - soc_end_frac;
    if (dod_frac >= 0.15f && dod_frac <= 0.95f && s->_ah_dis_session > 1.0f) {
        float q_meas = s->_ah_dis_session / dod_frac;
        q_meas = _clampf(q_meas, 0.3f * s->q_nominal_ah, 1.1f * s->q_nominal_ah);
        s->q_measured_ah = 0.92f * s->q_measured_ah + 0.08f * q_meas;
        s->cycle_count++;
    }

    if (dod_frac >= 0.15f &&
        s->_ah_dis_session > 1.0f &&
        r0_current > SOH_R0_MIN &&
        r0_current < SOH_R0_MAX) {
        float alpha = (s->r0_update_count < 4u) ? 0.18f : 0.06f;
        s->r0_ref_ohm = (1.0f - alpha) * s->r0_ref_ohm + alpha * r0_current;
        if (s->r0_update_count < UINT16_MAX) {
            s->r0_update_count++;
        }
    }

    s->_ah_dis_session = 0.0f;
    s->_wh_dis_session = 0.0f;
    s->_ah_chg_session = 0.0f;
    s->_wh_chg_session = 0.0f;
    s->_soc_dis_start_frac = _clampf(soc_end_frac, 0.0f, 1.0f);

    soh_calc(s);
    printf("[SOH] cycle end: DoD=%.0f%% q=%.2fAh r0=%.1fmOhm SOH=%.1f%% qC=%.2f rC=%.2f\n",
           dod_frac * 100.0f,
           s->q_measured_ah,
           s->r0_ref_ohm * 1000.0f,
           s->soh * 100.0f,
           s->soh_q_confidence,
           s->soh_r_confidence);
}

void soh_calc(BmsSoh *s) {
    float raw_soh_q = _clampf(s->q_measured_ah / s->q_nominal_ah, 0.01f, 1.0f);

    float raw_soh_r = 1.0f;
    if (s->r0_ref_ohm > (1.2f * s->r0_nominal_ohm)) {
        float r_ratio = s->r0_nominal_ohm / fmaxf(s->r0_ref_ohm, 0.001f);
        raw_soh_r = _clampf(r_ratio, 0.01f, 1.0f);
    }

    float q_conf = _clampf((float)s->cycle_count / 6.0f, 0.0f, 1.0f);
    float r_conf = 0.0f;
    if (s->r0_update_count >= 3u) {
        r_conf = _clampf((float)(s->r0_update_count - 2u) / 8.0f, 0.0f, 1.0f);
    }

    float soh_q_eff = 1.0f - (1.0f - raw_soh_q) * q_conf;
    float soh_r_eff = 1.0f - (1.0f - raw_soh_r) * r_conf;

    s->soh_q = raw_soh_q;
    s->soh_r = raw_soh_r;
    s->soh_q_confidence = q_conf;
    s->soh_r_confidence = r_conf;
    s->soh_confidence = _clampf(0.5f * q_conf + 0.5f * r_conf, 0.0f, 1.0f);
    s->soh = fminf(soh_q_eff, soh_r_eff);
}

int soh_rul_cycles(const BmsSoh *s) {
    float life_efc = 500.0f;
    float soh_eol = 0.80f;
    if (s->soh <= soh_eol) return 0;
    return (int)((s->soh - soh_eol) / (1.0f - soh_eol) * life_efc);
}
