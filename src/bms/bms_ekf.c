// ============================================================
// bms/bms_ekf.c — Extended Kalman Filter для SOC
// Модель: 1RC Thevenin, нелінійне OCV(SOC, T)
// ============================================================
#include "bms_ekf.h"
#include "bms_ocv.h"
#include <math.h>
#include <stdio.h>
#include <float.h>

// ── Утиліти ────────────────────────────────────────────────
static inline float _clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline bool _finite(float v) {
    return !isnan(v) && !isinf(v);
}

// R0 з температурною компенсацією Arrhenius
static float _r0_temp(const BmsEkf *ekf, float temp_c) {
    float t_k     = temp_c + 273.15f;
    float t_ref_k = EKF_T_REF + 273.15f;
    float factor  = expf(EKF_KR * (1.0f/t_k - 1.0f/t_ref_k));
    float r0t = ekf->r0 * factor;
    return _clamp(r0t, EKF_R0_MIN, EKF_R0_MAX * 2.0f);
}

// ── Ініціалізація ────────────────────────────────────────────
void ekf_init(BmsEkf *ekf, float soc_init) {
    ekf->soc   = _clamp(soc_init, 0.0f, 1.0f);
    ekf->v_rc  = 0.0f;
    ekf->r0    = EKF_R0_NOMINAL;
    ekf->r1    = EKF_R1_NOMINAL;
    ekf->c1    = EKF_C1_NOMINAL;
    ekf->q_n   = EKF_Q_NOMINAL;

    // Початкова невизначеність
    ekf->p00 = 0.025f * 0.025f;  // σ_SOC = 2.5%
    ekf->p01 = 0.0f;
    ekf->p11 = 0.001f * 0.001f;

    ekf->p00_init = ekf->p00;
    ekf->p11_init = ekf->p11;

    // Шуми процесу: дрейф SOC ≈ 0.02%/с; V_rc ≈ 0.1мВ/с
    ekf->q_soc   = 0.0002f * 0.0002f;
    ekf->q_vrc   = 0.0001f * 0.0001f;
    // Шум вимірювання напруги: ±10мВ
    ekf->r_noise = 0.010f * 0.010f;

    ekf->innovation = 0.0f;
    ekf->soc_std    = 0.025f;
    ekf->s_innov    = 1.0f;
}

// ── Один крок EKF ────────────────────────────────────────────
float ekf_step(BmsEkf *ekf,
               float i_pack, float v_term,
               float temp_c, float dt) {

    float soc = ekf->soc;
    float vrc = ekf->v_rc;

    // ── ПРОГНОЗ ──────────────────────────────────────────────
    float eta   = (i_pack < 0.0f) ? EKF_ETA_CHARGE : 1.0f;
    float tau   = ekf->r1 * ekf->c1;
    if (tau < 1.0f) tau = 1.0f;
    float alpha = expf(-dt / tau);

    float soc_pred = soc - eta * i_pack * dt / ekf->q_n;
    float vrc_pred = vrc * alpha + ekf->r1 * i_pack * (1.0f - alpha);

    // NaN guard
    if (!_finite(soc_pred)) soc_pred = soc;
    if (!_finite(vrc_pred)) vrc_pred = 0.0f;

    soc_pred = _clamp(soc_pred, 0.0f, 1.0f);
    vrc_pred = _clamp(vrc_pred, -0.5f, 0.5f);

    // P⁻ = F·P·Fᵀ + Q   (F = [[1,0],[0,α]])
    // F·P·Fᵀ:
    //   [p00,      p01*α ]
    //   [p01*α,    p11*α²]
    float p00_p = ekf->p00 + ekf->q_soc;
    float p01_p = ekf->p01 * alpha;
    float p11_p = ekf->p11 * alpha * alpha + ekf->q_vrc;

    // ── ОНОВЛЕННЯ ────────────────────────────────────────────
    // Прогноз V_terminal
    float ocv_pack = bms_ocv_pack(soc_pred, temp_c);
    float r0t      = _r0_temp(ekf, temp_c);
    float v_hat    = ocv_pack - i_pack * r0t - vrc_pred;

    float innov = v_term - v_hat;
    if (!_finite(innov)) innov = 0.0f;
    ekf->innovation = innov;

    // Якобіан H = [H0, H1]
    float H0 = bms_docv_dsoc(soc_pred, temp_c) * EKF_N_CELLS;
    float H1 = -1.0f;

    // S = H·P⁻·Hᵀ + R
    float HP0 = H0 * p00_p + H1 * p01_p;
    float HP1 = H0 * p01_p + H1 * p11_p;
    float S   = HP0 * H0 + HP1 * H1 + ekf->r_noise;
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
        return soc_pred;
    }

    // Захист від S ≈ 0
    if (S < 1e-10f) S = 1e-10f;

    // K = P⁻·Hᵀ / S
    float PHt0 = p00_p * H0 + p01_p * H1;
    float PHt1 = p01_p * H0 + p11_p * H1;
    float K0 = PHt0 / S;
    float K1 = PHt1 / S;

    // x = x⁻ + K·innov
    float soc_upd = soc_pred + K0 * innov;
    float vrc_upd = vrc_pred + K1 * innov;

    if (!_finite(soc_upd)) soc_upd = soc_pred;
    if (!_finite(vrc_upd)) vrc_upd = 0.0f;

    soc_upd = _clamp(soc_upd, 0.0f, 1.0f);
    vrc_upd = _clamp(vrc_upd, -0.5f, 0.5f);

    // P = (I − K·H)·P⁻   (Joseph form для симетрії)
    float IKH00 = 1.0f - K0 * H0;
    float IKH01 = -K0 * H1;
    float IKH10 = -K1 * H0;
    float IKH11 = 1.0f - K1 * H1;

    float np00 = IKH00 * p00_p + IKH01 * p01_p;
    float np01 = IKH00 * p01_p + IKH01 * p11_p;
    float np10 = IKH10 * p00_p + IKH11 * p01_p;
    float np11 = IKH10 * p01_p + IKH11 * p11_p;

    // Симетризація і захист від NaN
    ekf->p00 = _finite(np00) && np00 > 0.0f ? np00 : ekf->p00_init;
    ekf->p01 = _finite(np01) ? (np01 + np10) * 0.5f : 0.0f;
    ekf->p11 = _finite(np11) && np11 > 0.0f ? np11 : ekf->p11_init;

    // Мінімальні значення (числова стабільність)
    if (ekf->p00 < 1e-9f)  ekf->p00 = 1e-9f;
    if (ekf->p11 < 1e-12f) ekf->p11 = 1e-12f;

    // Перевірка розбіжності
    if (ekf->p00 > 1.0f || !_finite(ekf->p00)) {
        printf("[EKF] P diverged, resetting\n");
        ekf->p00 = ekf->p00_init * 10.0f;
        ekf->p01 = 0.0f;
        ekf->p11 = ekf->p11_init * 10.0f;
    }

    ekf->soc    = soc_upd;
    ekf->v_rc   = vrc_upd;
    ekf->soc_std = sqrtf(ekf->p00) * 100.0f;  // %

    return soc_upd;
}

// ── OCV-корекція ─────────────────────────────────────────────
void ekf_inject_ocv(BmsEkf *ekf, float v_ocv_pack, float temp_c,
                    float confidence) {
    float soc_ocv = bms_ocv_pack_to_soc(v_ocv_pack, temp_c);
    float w = _clamp(confidence, 0.0f, 1.0f);
    ekf->soc  = _clamp(w * soc_ocv + (1.0f - w) * ekf->soc, 0.0f, 1.0f);
    ekf->v_rc = 0.0f;  // поляризація знята у спокої
    ekf->p00 *= (1.0f - w * 0.9f);
    if (ekf->p00 < 1e-5f) ekf->p00 = 1e-5f;
    ekf->p11 = 1e-8f;
}

// ── Адаптація R0 ─────────────────────────────────────────────
void ekf_update_r0(BmsEkf *ekf, float delta_v, float delta_i) {
    if (fabsf(delta_i) < 0.5f) return;
    float r0_meas = fabsf(delta_v) / fabsf(delta_i);
    r0_meas = _clamp(r0_meas, EKF_R0_MIN, EKF_R0_MAX);
    // EMA α = 0.05
    ekf->r0 = 0.95f * ekf->r0 + 0.05f * r0_meas;
}
