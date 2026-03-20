// ============================================================
// bms/bms_ocv.c — LiPo OCV Table, Temp Compensation, Jacobian
// ============================================================
#include "bms_ocv.h"
#include <stddef.h>

#define N_CELLS     3
#define DVDT_CELL  -0.0015f    // В/°C/комірку
#define T_REF       25.0f

// OCV таблиця комірки при 25°C (SOC, OCV_V) — 28 точок
typedef struct { float soc; float ocv; } OcvPoint;

static const OcvPoint _OCV_TABLE[] = {
    {0.00f, 3.300f},
    {0.02f, 3.340f},
    {0.04f, 3.370f},
    {0.06f, 3.395f},
    {0.08f, 3.415f},
    {0.10f, 3.435f},
    {0.14f, 3.470f},
    {0.18f, 3.510f},
    {0.22f, 3.545f},
    {0.26f, 3.575f},
    {0.30f, 3.600f},
    {0.35f, 3.630f},
    {0.40f, 3.655f},
    {0.45f, 3.678f},
    {0.50f, 3.700f},
    {0.55f, 3.723f},
    {0.60f, 3.748f},
    {0.65f, 3.775f},
    {0.70f, 3.805f},
    {0.74f, 3.835f},
    {0.78f, 3.870f},
    {0.82f, 3.910f},
    {0.86f, 3.955f},
    {0.90f, 4.000f},
    {0.93f, 4.040f},
    {0.96f, 4.090f},
    {0.98f, 4.150f},
    {1.00f, 4.200f},
};

#define OCV_N ((int)(sizeof(_OCV_TABLE)/sizeof(_OCV_TABLE[0])))

// Лінійна інтерполяція по SOC
static float _interp_soc(float soc) {
    if (soc <= _OCV_TABLE[0].soc)      return _OCV_TABLE[0].ocv;
    if (soc >= _OCV_TABLE[OCV_N-1].soc) return _OCV_TABLE[OCV_N-1].ocv;
    // Двійковий пошук
    int lo = 0, hi = OCV_N - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) >> 1;
        if (_OCV_TABLE[mid].soc <= soc) lo = mid; else hi = mid;
    }
    float t = (soc - _OCV_TABLE[lo].soc) /
              (_OCV_TABLE[hi].soc - _OCV_TABLE[lo].soc);
    return _OCV_TABLE[lo].ocv + t * (_OCV_TABLE[hi].ocv - _OCV_TABLE[lo].ocv);
}

// Зворотня: OCV → SOC
static float _interp_ocv(float ocv) {
    if (ocv <= _OCV_TABLE[0].ocv)      return _OCV_TABLE[0].soc;
    if (ocv >= _OCV_TABLE[OCV_N-1].ocv) return _OCV_TABLE[OCV_N-1].soc;
    int lo = 0, hi = OCV_N - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) >> 1;
        if (_OCV_TABLE[mid].ocv <= ocv) lo = mid; else hi = mid;
    }
    float t = (ocv - _OCV_TABLE[lo].ocv) /
              (_OCV_TABLE[hi].ocv - _OCV_TABLE[lo].ocv);
    return _OCV_TABLE[lo].soc + t * (_OCV_TABLE[hi].soc - _OCV_TABLE[lo].soc);
}

float bms_ocv_cell(float soc, float temp_c) {
    if (soc < 0.0f) soc = 0.0f;
    if (soc > 1.0f) soc = 1.0f;
    float v25 = _interp_soc(soc);
    return v25 + DVDT_CELL * (temp_c - T_REF);
}

float bms_ocv_to_soc_cell(float ocv_v, float temp_c) {
    // Скоригувати до 25°C еквіваленту
    float ocv_corr = ocv_v - DVDT_CELL * (temp_c - T_REF);
    float soc = _interp_ocv(ocv_corr);
    if (soc < 0.0f) soc = 0.0f;
    if (soc > 1.0f) soc = 1.0f;
    return soc;
}

float bms_ocv_pack(float soc, float temp_c) {
    return bms_ocv_cell(soc, temp_c) * N_CELLS;
}

float bms_ocv_pack_to_soc(float v_pack, float temp_c) {
    return bms_ocv_to_soc_cell(v_pack / N_CELLS, temp_c);
}

float bms_docv_dsoc(float soc, float temp_c) {
    // Центральна різниця: h = 0.01
    float h   = 0.01f;
    float s_lo = (soc - h < 0.001f) ? 0.001f : soc - h;
    float s_hi = (soc + h > 0.999f) ? 0.999f : soc + h;
    float v_lo = bms_ocv_cell(s_lo, temp_c);
    float v_hi = bms_ocv_cell(s_hi, temp_c);
    return (v_hi - v_lo) / (s_hi - s_lo);
}
