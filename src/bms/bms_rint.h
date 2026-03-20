#pragma once
// ============================================================
// bms/bms_rint.h — Internal Resistance Model (Thevenin 1RC)
// R0(SOC,T,SOH) · R1(SOC,T) · C1(SOC) · Arrhenius temp comp
// ============================================================
#include <stdint.h>

typedef struct {
    float soh;       // 0.0–1.0 (масштабує R0)
    float r0;        // поточний R0 (адаптивний, Ом)
    float r1;        // R1 (Ом)
    float c1;        // C1 (Ф)
} RintModel;

void  rint_init(RintModel *m, float soh);
void  rint_update_soc(RintModel *m, float soc, float temp_c);
float rint_r0(const RintModel *m, float temp_c);
float rint_r1(const RintModel *m, float temp_c);
float rint_c1(const RintModel *m);
void  rint_adapt_r0(RintModel *m, float delta_v, float delta_i,
                    float alpha);
