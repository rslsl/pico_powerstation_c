#pragma once
// ============================================================
// bms/bms_ekf.h — Extended Kalman Filter (1RC Thevenin)
//
// Вектор стану:  x = [SOC (0-1), V_rc (В)]ᵀ
// Спостереження: y = V_terminal (В)
// ============================================================
#include <stdint.h>
#include <stdbool.h>

// Параметри еквівалентної схеми (3S2P LiPo 34Ah, 25°C)
#define EKF_R0_NOMINAL  0.020f   // Ом
#define EKF_R0_MIN      0.005f
#define EKF_R0_MAX      0.200f
#define EKF_R1_NOMINAL  0.010f   // Ом
#define EKF_C1_NOMINAL  3000.0f  // Ф  (τ = R1·C1 = 30с)
#define EKF_Q_NOMINAL   (34.0f * 3600.0f)  // А·с
#define EKF_ETA_CHARGE  0.97f
#define EKF_N_CELLS     3
#define EKF_KR          0.035f   // 1/°C Arrhenius R0
#define EKF_T_REF       25.0f    // °C

typedef struct {
    // Стан
    float soc;      // State of Charge 0.0–1.0
    float v_rc;     // RC поляризаційна напруга (В)

    // Коваріаційна матриця P [2×2] (симетрична)
    float p00, p01, p11;

    // Параметри схеми
    float r0;       // поточний R0 (адаптивний)
    float r1;
    float c1;
    float q_n;      // ємність (А·с)

    // Шуми
    float q_soc;    // дисперсія процесу SOC
    float q_vrc;    // дисперсія процесу V_rc
    float r_noise;  // дисперсія вимірювання

    // Діагностика
    float innovation;
    float soc_std;
    float s_innov;

    // Початкові Q для скидання P
    float p00_init, p11_init;
} BmsEkf;

void  ekf_init(BmsEkf *ekf, float soc_init);
float ekf_step(BmsEkf *ekf,
               float i_pack,   // А (+ = розряд)
               float v_term,   // В виміряна
               float temp_c,
               float dt);      // с

// Корекція за OCV у режимі спокою
void  ekf_inject_ocv(BmsEkf *ekf, float v_ocv_pack, float temp_c,
                     float confidence);

// Адаптивне оновлення R0 за HPPC-пульсом
void  ekf_update_r0(BmsEkf *ekf, float delta_v, float delta_i);
