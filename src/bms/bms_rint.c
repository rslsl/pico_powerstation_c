// ============================================================
// bms/bms_rint.c — Thevenin 1RC Internal Resistance
// ============================================================
#include "bms_rint.h"
#include "bms_ekf.h"   // EKF_R0_MIN/MAX, EKF_T_REF, EKF_KR
#include <math.h>
#include <stddef.h>

// SOC→R0 карта (3S2P LiPo, 25°C, Ом)
typedef struct { float soc; float val; } MapPt;

static const MapPt _R0[] = {
    {0.00f,0.038f},{0.05f,0.032f},{0.10f,0.026f},{0.20f,0.022f},
    {0.30f,0.020f},{0.40f,0.019f},{0.50f,0.018f},{0.60f,0.018f},
    {0.70f,0.019f},{0.80f,0.020f},{0.90f,0.022f},{1.00f,0.025f},
};
static const MapPt _R1[] = {
    {0.00f,0.018f},{0.10f,0.014f},{0.30f,0.010f},
    {0.50f,0.009f},{0.70f,0.010f},{0.90f,0.012f},{1.00f,0.015f},
};
static const MapPt _C1[] = {
    {0.00f,2000.f},{0.20f,2500.f},{0.50f,3500.f},
    {0.80f,2800.f},{1.00f,2200.f},
};

#define ARR_N(a) (int)(sizeof(a)/sizeof((a)[0]))

static float _interp(float x, const MapPt *t, int n) {
    if (x <= t[0].soc)    return t[0].val;
    if (x >= t[n-1].soc)  return t[n-1].val;
    int lo=0, hi=n-1;
    while (hi-lo>1) { int m=(lo+hi)>>1; if(t[m].soc<=x) lo=m; else hi=m; }
    float f = (x-t[lo].soc)/(t[hi].soc-t[lo].soc);
    return t[lo].val + f*(t[hi].val-t[lo].val);
}

static float _arrhenius(float r_ref, float temp_c) {
    float t_k = temp_c + 273.15f, t_ref = EKF_T_REF + 273.15f;
    return r_ref * expf(EKF_KR * (1.0f/t_k - 1.0f/t_ref));
}

void rint_init(RintModel *m, float soh) {
    m->soh = soh;
    m->r0  = 0.020f;
    m->r1  = 0.010f;
    m->c1  = 3000.0f;
}

void rint_update_soc(RintModel *m, float soc, float temp_c) {
    float r0_25 = _interp(soc, _R0, ARR_N(_R0));
    // SOH деградація: R0 зростає обернено пропорційно SOH
    if (m->soh > 0.01f) r0_25 /= m->soh;
    m->r0 = _arrhenius(r0_25, temp_c);
    m->r1 = _arrhenius(_interp(soc, _R1, ARR_N(_R1)), temp_c);
    m->c1 = _interp(soc, _C1, ARR_N(_C1));
}

float rint_r0(const RintModel *m, float temp_c) {
    (void)temp_c;
    return m->r0;
}
float rint_r1(const RintModel *m, float temp_c) {
    (void)temp_c;
    return m->r1;
}
float rint_c1(const RintModel *m) { return m->c1; }

void rint_adapt_r0(RintModel *m, float delta_v, float delta_i, float alpha) {
    if (fabsf(delta_i) < 0.5f) return;
    float r_meas = fabsf(delta_v) / fabsf(delta_i);
    if (r_meas < EKF_R0_MIN || r_meas > EKF_R0_MAX) return;
    m->r0 = (1.0f - alpha) * m->r0 + alpha * r_meas;
}
