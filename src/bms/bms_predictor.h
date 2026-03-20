#pragma once
// ============================================================
// bms/bms_predictor.h - Runtime remaining-time predictor
// Single-source discharge TTE estimator with persistent baseline
// ============================================================
#include <stdint.h>

#define PRED_REG_SIZE        30
#define PRED_WIN_SIZE        50
#define PRED_WARMUP_SAMPLES  30
#define PRED_PEUKERT_DEFAULT 1.04f
#define PRED_I_NOMINAL_A     3.0f

typedef struct {
    float q_nominal_ah;
    float v_nominal;

    // Power EMA
    float _p_ema;
    int   _ema_n;

    // Moving window
    float _p_win[PRED_WIN_SIZE];
    int   _win_ptr;
    int   _win_n;

    // Linear regression over remaining energy
    float _reg_t[PRED_REG_SIZE];
    float _reg_e[PRED_REG_SIZE];
    int   _reg_ptr;
    int   _reg_n;
    float _t_elapsed;

    // Persistent priors
    float _peukert_n;
    float _baseline_power_w;
    uint16_t _warmup_samples;

    // Results
    int   minutes_ema;
    int   minutes_wind;
    int   minutes_reg;
    int   minutes_best;
    float confidence;
} Predictor;

void pred_init(Predictor *p, float q_nominal_ah, float v_nominal);
void pred_seed(Predictor *p, float baseline_power_w, float peukert_n);
int  pred_update(Predictor *p,
                 float soc,
                 float i_pack,
                 float power_w,
                 float energy_remain_wh,
                 float temp_c,
                 float soh,
                 float r0_ohm);
