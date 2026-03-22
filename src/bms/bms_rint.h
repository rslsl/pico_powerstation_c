#pragma once

#include <stdint.h>

typedef struct {
    float soh;
    float r0;
    float r1;
    float c1;
    float r0_scale;
    float r0_25_base;
} RintModel;

void  rint_init(RintModel *m, float soh);
void  rint_update_soc(RintModel *m, float soc, float temp_c);
float rint_r0(const RintModel *m, float temp_c);
float rint_r1(const RintModel *m, float temp_c);
float rint_c1(const RintModel *m);
void  rint_adapt_r0(RintModel *m, float delta_v, float delta_i, float alpha);
