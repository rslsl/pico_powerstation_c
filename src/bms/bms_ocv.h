#pragma once
// ============================================================
// bms/bms_ocv.h — OCV Table + Temperature Compensation
// ============================================================
#include <stdint.h>

// SOC (0.0–1.0) → OCV однієї комірки LiPo (В) при temp_c
float bms_ocv_cell(float soc_frac, float temp_c);

// OCV комірки (В) → SOC (0.0–1.0), temp-compensated
float bms_ocv_to_soc_cell(float ocv_v, float temp_c);

// SOC → OCV пакету 3S (В)
float bms_ocv_pack(float soc_frac, float temp_c);

// OCV пакету 3S → SOC
float bms_ocv_pack_to_soc(float v_pack, float temp_c);

// dOCV/dSOC чисельна похідна для Якобіана EKF (В/unit)
float bms_docv_dsoc(float soc_frac, float temp_c);
