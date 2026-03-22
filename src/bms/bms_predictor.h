#pragma once
// ============================================================
// bms/bms_predictor.h - Runtime remaining-time predictor
// Single-source discharge TTE estimator with persistent baseline
// ============================================================
#include <stdint.h>

#define PRED_REG_SIZE           120
#define PRED_WIN_SIZE           200
#define PRED_WARMUP_SECONDS     8.0f
#define PRED_RESET_IDLE_SECONDS 20.0f
#define PRED_PEUKERT_DEFAULT    1.04f
#define PRED_I_NOMINAL_A        3.0f
#define PRED_EMA_ALPHA          0.02f
#define PRED_OUTPUT_EMA_ALPHA   0.08f

typedef struct {
    float q_nominal_ah;
    float v_nominal;

    // Fast and slow power estimators
    float _p_ema;
    float _p_fast;
    float _p_slow;

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
    float _warmup_s;
    float _minutes_smoothed;

    // Results
    int   minutes_ema;
    int   minutes_fast;
    int   minutes_slow;
    int   minutes_wind;
    int   minutes_reg;
    int   minutes_raw;
    int   minutes_display;
    int   minutes_best;
    float power_fast_w;
    float power_slow_w;
    float power_window_w;
    float power_cons_w;
    float usable_wh;
    float confidence;
} Predictor;

void pred_init(Predictor *p, float q_nominal_ah, float v_nominal);
void pred_seed(Predictor *p, float baseline_power_w, float peukert_n);
void pred_reset_runtime(Predictor *p);
int  pred_update(Predictor *p,
                 float soc,
                 float i_pack,
                 float power_w,
                 float energy_remain_wh,
                 float temp_c,
                 float soh,
                 float r0_ohm,
                 float dt_s);
