#include "bms_rint.h"
#include "bms_ekf.h"
#include <math.h>
#include <stddef.h>

typedef struct {
    float soc;
    float val;
} MapPt;

static const MapPt kR0Map[] = {
    {0.00f, 0.038f}, {0.05f, 0.032f}, {0.10f, 0.026f}, {0.20f, 0.022f},
    {0.30f, 0.020f}, {0.40f, 0.019f}, {0.50f, 0.018f}, {0.60f, 0.018f},
    {0.70f, 0.019f}, {0.80f, 0.020f}, {0.90f, 0.022f}, {1.00f, 0.025f},
};

static const MapPt kR1Map[] = {
    {0.00f, 0.018f}, {0.10f, 0.014f}, {0.30f, 0.010f}, {0.50f, 0.009f},
    {0.70f, 0.010f}, {0.90f, 0.012f}, {1.00f, 0.015f},
};

static const MapPt kC1Map[] = {
    {0.00f, 2000.0f}, {0.20f, 2500.0f}, {0.50f, 3500.0f},
    {0.80f, 2800.0f}, {1.00f, 2200.0f},
};

#define ARR_N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static inline float _clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float _interp(float x, const MapPt *table, int n) {
    if (x <= table[0].soc) return table[0].val;
    if (x >= table[n - 1].soc) return table[n - 1].val;

    int lo = 0;
    int hi = n - 1;
    while ((hi - lo) > 1) {
        int mid = (lo + hi) >> 1;
        if (table[mid].soc <= x) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    float frac = (x - table[lo].soc) / (table[hi].soc - table[lo].soc);
    return table[lo].val + frac * (table[hi].val - table[lo].val);
}

static float _arrhenius(float r_ref, float temp_c) {
    float t_k = temp_c + 273.15f;
    float t_ref_k = EKF_T_REF + 273.15f;
    float factor = expf(EKF_KR * (1.0f / t_k - 1.0f / t_ref_k));
    return r_ref * factor;
}

void rint_init(RintModel *m, float soh) {
    m->soh = _clamp(soh, 0.30f, 1.0f);
    m->r0 = BAT_R0_NOMINAL_OHM;
    m->r1 = EKF_R1_NOMINAL;
    m->c1 = EKF_C1_NOMINAL;
    m->r0_scale = 1.0f;
    m->r0_25_base = BAT_R0_NOMINAL_OHM;
}

void rint_update_soc(RintModel *m, float soc, float temp_c) {
    float soc_clamped = _clamp(soc, 0.0f, 1.0f);
    float soh = _clamp(m->soh, 0.30f, 1.0f);

    float r0_25 = _interp(soc_clamped, kR0Map, ARR_N(kR0Map));
    r0_25 = (r0_25 / soh) * _clamp(m->r0_scale, 0.60f, 1.80f);
    m->r0_25_base = r0_25;
    m->r0 = _clamp(_arrhenius(r0_25, temp_c), EKF_R0_MIN, EKF_R0_MAX);
    m->r1 = _clamp(_arrhenius(_interp(soc_clamped, kR1Map, ARR_N(kR1Map)), temp_c),
                   0.001f, 0.200f);
    m->c1 = _clamp(_interp(soc_clamped, kC1Map, ARR_N(kC1Map)), 500.0f, 12000.0f);
}

float rint_r0(const RintModel *m, float temp_c) {
    (void)temp_c;
    return m->r0;
}

float rint_r1(const RintModel *m, float temp_c) {
    (void)temp_c;
    return m->r1;
}

float rint_c1(const RintModel *m) {
    return m->c1;
}

void rint_adapt_r0(RintModel *m, float delta_v, float delta_i, float alpha) {
    if (fabsf(delta_i) < 0.5f) return;

    float r_meas = fabsf(delta_v) / fabsf(delta_i);
    if (!isfinite(r_meas) || r_meas < EKF_R0_MIN || r_meas > EKF_R0_MAX) return;

    alpha = _clamp(alpha, 0.0f, 0.5f);
    float scale_target = r_meas / fmaxf(m->r0, EKF_R0_MIN);
    scale_target = _clamp(scale_target, 0.85f, 1.15f);

    float scale_step = (1.0f - alpha) + alpha * scale_target;
    m->r0_scale = _clamp(m->r0_scale * scale_step, 0.60f, 1.80f);
    m->r0 = _clamp(m->r0 * scale_step, EKF_R0_MIN, EKF_R0_MAX);
}
