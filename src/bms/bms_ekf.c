#include "bms_ekf.h"
#include "bms_ocv.h"
#include <math.h>
#include <stdio.h>

static inline float _clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline bool _finite(float v) {
    return !isnan(v) && !isinf(v);
}

static void _sync_rint_model(BmsEkf *ekf, float temp_c) {
    ekf->r0 = _clamp(rint_r0(&ekf->rint, temp_c), EKF_R0_MIN, EKF_R0_MAX);
    ekf->r1 = _clamp(rint_r1(&ekf->rint, temp_c), 0.001f, 0.200f);
    ekf->c1 = _clamp(rint_c1(&ekf->rint), 500.0f, 12000.0f);
}

static void _reset_r0_innovation_adapt(BmsEkf *ekf) {
    ekf->innov_mismatch_count = 0u;
    ekf->innov_mismatch_sign = 0;
    ekf->r0_scale_session_base = _clamp(ekf->rint.r0_scale, 0.60f, 1.80f);
}

static void _adapt_r0_from_innovation(BmsEkf *ekf, float i_pack, float temp_c) {
    float abs_innov = fabsf(ekf->innovation);
    if (ekf->innov_abs_ema <= 0.0f) {
        ekf->innov_abs_ema = abs_innov;
    } else {
        ekf->innov_abs_ema = 0.85f * ekf->innov_abs_ema + 0.15f * abs_innov;
    }

    if (i_pack <= 1.0f || ekf->innov_abs_ema < 0.050f) {
        _reset_r0_innovation_adapt(ekf);
        return;
    }

    int8_t sign = (ekf->innovation > 0.0f) ? 1 : -1;
    if (sign == ekf->innov_mismatch_sign) {
        if (ekf->innov_mismatch_count < UINT16_MAX) {
            ekf->innov_mismatch_count++;
        }
    } else {
        ekf->innov_mismatch_sign = sign;
        ekf->innov_mismatch_count = 1u;
        ekf->r0_scale_session_base = _clamp(ekf->rint.r0_scale, 0.60f, 1.80f);
    }

    if (ekf->innov_mismatch_count < 10u) {
        return;
    }

    float scale = (sign > 0) ? 0.995f : 1.005f;
    float base = _clamp(ekf->r0_scale_session_base, 0.60f, 1.80f);
    ekf->rint.r0_scale = _clamp(ekf->rint.r0_scale * scale, base * 0.80f, base * 1.20f);
    rint_update_soc(&ekf->rint, ekf->soc, temp_c);
    _sync_rint_model(ekf, temp_c);
}

void ekf_init(BmsEkf *ekf, float soc_init) {
    ekf->soc = _clamp(soc_init, 0.0f, 1.0f);
    ekf->v_rc = 0.0f;
    ekf->q_n = EKF_Q_NOMINAL;

    ekf->p00 = 0.025f * 0.025f;
    ekf->p01 = 0.0f;
    ekf->p11 = 0.001f * 0.001f;
    ekf->p00_init = ekf->p00;
    ekf->p11_init = ekf->p11;

    ekf->q_soc = 0.0002f * 0.0002f;
    ekf->q_vrc = 0.0001f * 0.0001f;
    ekf->r_noise = 0.010f * 0.010f;

    ekf->innovation = 0.0f;
    ekf->soc_std = 0.025f;
    ekf->s_innov = 1.0f;
    ekf->innov_abs_ema = 0.0f;

    rint_init(&ekf->rint, 1.0f);
    rint_update_soc(&ekf->rint, ekf->soc, EKF_T_REF);
    _sync_rint_model(ekf, EKF_T_REF);
    _reset_r0_innovation_adapt(ekf);
}

float ekf_step(BmsEkf *ekf,
               float i_pack,
               float v_term,
               float temp_c,
               float dt) {
    if (!_finite(dt) || dt <= 0.0f) dt = 0.1f;
    if (!_finite(temp_c)) temp_c = EKF_T_REF;

    float soc = ekf->soc;
    float vrc = ekf->v_rc;
    float eta = (i_pack < 0.0f) ? EKF_ETA_CHARGE : 1.0f;

    float soc_pred = soc - eta * i_pack * dt / ekf->q_n;
    if (!_finite(soc_pred)) soc_pred = soc;
    soc_pred = _clamp(soc_pred, 0.0f, 1.0f);

    rint_update_soc(&ekf->rint, soc_pred, temp_c);
    _sync_rint_model(ekf, temp_c);

    float tau = ekf->r1 * ekf->c1;
    if (!_finite(tau) || tau < 1.0f) tau = 1.0f;
    float alpha = expf(-dt / tau);
    float vrc_pred = vrc * alpha + ekf->r1 * i_pack * (1.0f - alpha);
    if (!_finite(vrc_pred)) vrc_pred = 0.0f;
    vrc_pred = _clamp(vrc_pred, -0.5f, 0.5f);

    float p00_p = ekf->p00 + ekf->q_soc;
    float p01_p = ekf->p01 * alpha;
    float p11_p = ekf->p11 * alpha * alpha + ekf->q_vrc;

    float ocv_pack = bms_ocv_pack(soc_pred, temp_c);
    float v_hat = ocv_pack - i_pack * ekf->r0 - vrc_pred;
    float innov = v_term - v_hat;
    if (!_finite(innov)) innov = 0.0f;
    ekf->innovation = innov;

    float H0 = bms_docv_dsoc(soc_pred, temp_c) * EKF_N_CELLS;
    float H1 = -1.0f;
    float HP0 = H0 * p00_p + H1 * p01_p;
    float HP1 = H0 * p01_p + H1 * p11_p;
    float S = HP0 * H0 + HP1 * H1 + ekf->r_noise;
    ekf->s_innov = S;
    if (!_finite(H0) || !_finite(HP0) || !_finite(HP1) || !_finite(S) || S <= 1e-10f) {
        ekf->soc = soc_pred;
        ekf->v_rc = vrc_pred;
        ekf->p00 = (_finite(p00_p) && p00_p > 0.0f) ? p00_p : ekf->p00_init;
        ekf->p01 = _finite(p01_p) ? p01_p : 0.0f;
        ekf->p11 = (_finite(p11_p) && p11_p > 0.0f) ? p11_p : ekf->p11_init;
        ekf->innovation = 0.0f;
        ekf->s_innov = 1.0f;
        ekf->soc_std = sqrtf(ekf->p00) * 100.0f;
        _reset_r0_innovation_adapt(ekf);
        return soc_pred;
    }

    if (S < 1e-10f) S = 1e-10f;

    float PHt0 = p00_p * H0 + p01_p * H1;
    float PHt1 = p01_p * H0 + p11_p * H1;
    float K0 = PHt0 / S;
    float K1 = PHt1 / S;

    float soc_upd = soc_pred + K0 * innov;
    float vrc_upd = vrc_pred + K1 * innov;
    if (!_finite(soc_upd)) soc_upd = soc_pred;
    if (!_finite(vrc_upd)) vrc_upd = 0.0f;

    soc_upd = _clamp(soc_upd, 0.0f, 1.0f);
    vrc_upd = _clamp(vrc_upd, -0.5f, 0.5f);

    float IKH00 = 1.0f - K0 * H0;
    float IKH01 = -K0 * H1;
    float IKH10 = -K1 * H0;
    float IKH11 = 1.0f - K1 * H1;

    float np00 = IKH00 * p00_p + IKH01 * p01_p;
    float np01 = IKH00 * p01_p + IKH01 * p11_p;
    float np10 = IKH10 * p00_p + IKH11 * p01_p;
    float np11 = IKH10 * p01_p + IKH11 * p11_p;

    ekf->p00 = (_finite(np00) && np00 > 0.0f) ? np00 : ekf->p00_init;
    ekf->p01 = _finite(np01) ? (np01 + np10) * 0.5f : 0.0f;
    ekf->p11 = (_finite(np11) && np11 > 0.0f) ? np11 : ekf->p11_init;
    if (ekf->p00 < 1e-9f) ekf->p00 = 1e-9f;
    if (ekf->p11 < 1e-12f) ekf->p11 = 1e-12f;

    if (ekf->p00 > 1.0f || !_finite(ekf->p00)) {
        printf("[EKF] P diverged, resetting\n");
        ekf->p00 = ekf->p00_init * 10.0f;
        ekf->p01 = 0.0f;
        ekf->p11 = ekf->p11_init * 10.0f;
    }

    ekf->soc = soc_upd;
    ekf->v_rc = vrc_upd;
    ekf->soc_std = sqrtf(ekf->p00) * 100.0f;

    rint_update_soc(&ekf->rint, ekf->soc, temp_c);
    _sync_rint_model(ekf, temp_c);
    _adapt_r0_from_innovation(ekf, i_pack, temp_c);
    return soc_upd;
}

void ekf_inject_ocv(BmsEkf *ekf, float v_ocv_pack, float temp_c, float confidence) {
    float soc_ocv = bms_ocv_pack_to_soc(v_ocv_pack, temp_c);
    float w = _clamp(confidence, 0.0f, 1.0f);
    ekf->soc = _clamp(w * soc_ocv + (1.0f - w) * ekf->soc, 0.0f, 1.0f);
    ekf->v_rc = 0.0f;
    ekf->p00 *= (1.0f - w * 0.9f);
    if (ekf->p00 < 1e-5f) ekf->p00 = 1e-5f;
    ekf->p11 = 1e-8f;

    rint_update_soc(&ekf->rint, ekf->soc, temp_c);
    _sync_rint_model(ekf, temp_c);
    _reset_r0_innovation_adapt(ekf);
}

void ekf_update_r0(BmsEkf *ekf, float delta_v, float delta_i, float temp_c) {
    if (fabsf(delta_i) < 0.5f) return;
    rint_adapt_r0(&ekf->rint, delta_v, delta_i, 0.05f);
    rint_update_soc(&ekf->rint, ekf->soc, temp_c);
    _sync_rint_model(ekf, temp_c);
}
