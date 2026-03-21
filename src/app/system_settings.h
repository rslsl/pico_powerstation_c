#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SETTINGS_MAGIC   0x53595453u
#define SETTINGS_VERSION 2u

typedef struct __attribute__((packed)) {
    uint16_t version;

    float capacity_ah;
    float pack_full_v;
    float pack_empty_v;
    float pack_nominal_v;

    float cell_warn_v;
    float cell_cut_v;
    float vbat_warn_v;
    float vbat_cut_v;

    float temp_bat_warn_c;
    float temp_bat_buzz_c;
    float temp_bat_safe_c;
    float temp_bat_cut_c;
    float temp_bat_charge_min_c;
    float temp_inv_warn_c;
    float temp_inv_safe_c;
    float temp_inv_cut_c;

    float shunt_dis_mohm;
    float shunt_chg_mohm;
    float pack_dis_v_gain;
    float pack_chg_v_gain;
    float cell1_v_gain;
    float cell2_v_gain;
    float cell3_v_gain;

    float cal_ref_dis_current_a;
    float cal_ref_dis_voltage_v;
    float cal_ref_chg_current_a;
    float cal_ref_chg_voltage_v;
    float cal_ref_cell1_v;
    float cal_ref_cell2_v;
    float cal_ref_cell3_v;

    uint8_t ui_brightness;
    uint8_t buzzer_en;
    uint8_t _pad[2];
} SystemSettings;

_Static_assert(sizeof(SystemSettings) <= 4080, "SystemSettings payload too large");

void settings_init(void);
void settings_defaults(SystemSettings *out);
void settings_copy(SystemSettings *out);
const SystemSettings *settings_get(void);
bool settings_store(const SystemSettings *next);
bool settings_reset_defaults(void);
