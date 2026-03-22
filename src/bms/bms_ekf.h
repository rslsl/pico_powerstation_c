#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../config.h"
#include "bms_rint.h"

#define EKF_R0_NOMINAL  BAT_R0_NOMINAL_OHM
#define EKF_R0_MIN      0.005f
#define EKF_R0_MAX      0.200f
#define EKF_R1_NOMINAL  0.010f
#define EKF_C1_NOMINAL  3000.0f
#define EKF_Q_NOMINAL   (34.0f * 3600.0f)
#define EKF_ETA_CHARGE  0.97f
#define EKF_N_CELLS     3
#define EKF_KR          0.035f
#define EKF_T_REF       25.0f

typedef struct {
    float soc;
    float v_rc;

    float p00, p01, p11;

    float r0;
    float r1;
    float c1;
    float q_n;

    float q_soc;
    float q_vrc;
    float r_noise;

    float innovation;
    float soc_std;
    float s_innov;

    float p00_init;
    float p11_init;

    RintModel rint;
    float innov_abs_ema;
    float r0_scale_session_base;
    uint16_t innov_mismatch_count;
    int8_t innov_mismatch_sign;
} BmsEkf;

void  ekf_init(BmsEkf *ekf, float soc_init);
float ekf_step(BmsEkf *ekf,
               float i_pack,
               float v_term,
               float temp_c,
               float dt);

void  ekf_inject_ocv(BmsEkf *ekf, float v_ocv_pack, float temp_c,
                     float confidence);
void  ekf_update_r0(BmsEkf *ekf, float delta_v, float delta_i, float temp_c);
