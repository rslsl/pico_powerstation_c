#include "system_settings.h"

#include "../bms/flash_nvm.h"
#include "../config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static SystemSettings g_settings;
static uint32_t g_settings_seq = 0;
static uint8_t g_settings_slot = 0;
static bool g_settings_ready = false;

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

    uint8_t ui_brightness;
    uint8_t buzzer_en;
    uint8_t _pad[2];
} SystemSettingsV1;

static float _clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool _finitef(float v) {
    return !isnan(v) && !isinf(v);
}

static void _settings_from_v1(SystemSettings *dst, const SystemSettingsV1 *src) {
    settings_defaults(dst);

    dst->capacity_ah            = src->capacity_ah;
    dst->pack_full_v            = src->pack_full_v;
    dst->pack_empty_v           = src->pack_empty_v;
    dst->pack_nominal_v         = src->pack_nominal_v;
    dst->cell_warn_v            = src->cell_warn_v;
    dst->cell_cut_v             = src->cell_cut_v;
    dst->vbat_warn_v            = src->vbat_warn_v;
    dst->vbat_cut_v             = src->vbat_cut_v;
    dst->temp_bat_warn_c        = src->temp_bat_warn_c;
    dst->temp_bat_buzz_c        = src->temp_bat_buzz_c;
    dst->temp_bat_safe_c        = src->temp_bat_safe_c;
    dst->temp_bat_cut_c         = src->temp_bat_cut_c;
    dst->temp_bat_charge_min_c  = src->temp_bat_charge_min_c;
    dst->temp_inv_warn_c        = src->temp_inv_warn_c;
    dst->temp_inv_safe_c        = src->temp_inv_safe_c;
    dst->temp_inv_cut_c         = src->temp_inv_cut_c;
    dst->shunt_dis_mohm         = src->shunt_dis_mohm;
    dst->shunt_chg_mohm         = src->shunt_chg_mohm;
    dst->ui_brightness          = src->ui_brightness;
    dst->buzzer_en              = src->buzzer_en;
}

void settings_defaults(SystemSettings *out) {
    memset(out, 0, sizeof(*out));
    out->version = SETTINGS_VERSION;

    out->capacity_ah = BAT_CAPACITY_AH;
    out->pack_full_v = BAT_FULL_V;
    out->pack_empty_v = BAT_EMPTY_V;
    out->pack_nominal_v = BAT_NOMINAL_V;

    out->cell_warn_v = CELL_WARN_V;
    out->cell_cut_v = CELL_CUT_V;
    out->vbat_warn_v = VBAT_WARN_V;
    out->vbat_cut_v = VBAT_CUT_V;

    out->temp_bat_warn_c = TEMP_BAT_WARN_C;
    out->temp_bat_buzz_c = TEMP_BAT_BUZZ_C;
    out->temp_bat_safe_c = TEMP_BAT_SAFE_C;
    out->temp_bat_cut_c = TEMP_BAT_CUT_C;
    out->temp_bat_charge_min_c = TEMP_BAT_CHARGE_MIN_C;
    out->temp_inv_warn_c = TEMP_INV_WARN_C;
    out->temp_inv_safe_c = TEMP_INV_SAFE_C;
    out->temp_inv_cut_c = TEMP_INV_CUT_C;

    out->shunt_dis_mohm = SHUNT_DIS_MOHM;
    out->shunt_chg_mohm = SHUNT_CHG_MOHM;
    out->pack_dis_v_gain = 1.0f;
    out->pack_chg_v_gain = 1.0f;
    out->cell1_v_gain = 1.0f;
    out->cell2_v_gain = 1.0f;
    out->cell3_v_gain = 1.0f;

    out->cal_ref_dis_current_a = 0.0f;
    out->cal_ref_dis_voltage_v = 0.0f;
    out->cal_ref_chg_current_a = 0.0f;
    out->cal_ref_chg_voltage_v = 0.0f;
    out->cal_ref_cell1_v = 0.0f;
    out->cal_ref_cell2_v = 0.0f;
    out->cal_ref_cell3_v = 0.0f;

    out->ui_brightness = UI_BL_ACTIVE_PCT;
    out->buzzer_en = 1u;
}

static void _settings_sanitize(SystemSettings *s) {
    SystemSettings def;
    settings_defaults(&def);

    if (s->version != SETTINGS_VERSION) {
        s->version = SETTINGS_VERSION;
    }

    if (!_finitef(s->capacity_ah)) s->capacity_ah = def.capacity_ah;
    s->capacity_ah = _clampf(s->capacity_ah, 1.0f, 120.0f);

    if (!_finitef(s->pack_full_v)) s->pack_full_v = def.pack_full_v;
    if (!_finitef(s->pack_empty_v)) s->pack_empty_v = def.pack_empty_v;
    if (!_finitef(s->pack_nominal_v)) s->pack_nominal_v = def.pack_nominal_v;
    s->pack_full_v = _clampf(s->pack_full_v, 11.0f, 13.2f);
    s->pack_empty_v = _clampf(s->pack_empty_v, 8.5f, 11.5f);
    if (s->pack_empty_v > s->pack_full_v - 0.8f) {
        s->pack_empty_v = s->pack_full_v - 0.8f;
    }
    s->pack_nominal_v = _clampf(s->pack_nominal_v,
                                s->pack_empty_v + 0.3f,
                                s->pack_full_v - 0.3f);

    if (!_finitef(s->cell_cut_v)) s->cell_cut_v = def.cell_cut_v;
    if (!_finitef(s->cell_warn_v)) s->cell_warn_v = def.cell_warn_v;
    s->cell_cut_v = _clampf(s->cell_cut_v, 2.80f, 3.80f);
    s->cell_warn_v = _clampf(s->cell_warn_v, 3.00f, 4.10f);
    if (s->cell_warn_v < s->cell_cut_v + 0.05f) {
        s->cell_warn_v = s->cell_cut_v + 0.05f;
    }

    if (!_finitef(s->vbat_cut_v)) s->vbat_cut_v = def.vbat_cut_v;
    if (!_finitef(s->vbat_warn_v)) s->vbat_warn_v = def.vbat_warn_v;
    s->vbat_cut_v = _clampf(s->vbat_cut_v, 8.5f, 11.5f);
    s->vbat_warn_v = _clampf(s->vbat_warn_v, 9.0f, 12.3f);
    if (s->vbat_warn_v < s->vbat_cut_v + 0.2f) {
        s->vbat_warn_v = s->vbat_cut_v + 0.2f;
    }

    if (!_finitef(s->temp_bat_warn_c)) s->temp_bat_warn_c = def.temp_bat_warn_c;
    if (!_finitef(s->temp_bat_buzz_c)) s->temp_bat_buzz_c = def.temp_bat_buzz_c;
    if (!_finitef(s->temp_bat_safe_c)) s->temp_bat_safe_c = def.temp_bat_safe_c;
    if (!_finitef(s->temp_bat_cut_c)) s->temp_bat_cut_c = def.temp_bat_cut_c;
    if (!_finitef(s->temp_bat_charge_min_c)) s->temp_bat_charge_min_c = def.temp_bat_charge_min_c;
    s->temp_bat_warn_c = _clampf(s->temp_bat_warn_c, -10.0f, 80.0f);
    s->temp_bat_buzz_c = _clampf(s->temp_bat_buzz_c, -10.0f, 85.0f);
    s->temp_bat_safe_c = _clampf(s->temp_bat_safe_c, 10.0f, 90.0f);
    s->temp_bat_cut_c = _clampf(s->temp_bat_cut_c, 15.0f, 95.0f);
    s->temp_bat_charge_min_c = _clampf(s->temp_bat_charge_min_c, -20.0f, 20.0f);
    if (s->temp_bat_buzz_c < s->temp_bat_warn_c) {
        s->temp_bat_buzz_c = s->temp_bat_warn_c;
    }
    if (s->temp_bat_safe_c < s->temp_bat_buzz_c + 1.0f) {
        s->temp_bat_safe_c = s->temp_bat_buzz_c + 1.0f;
    }
    if (s->temp_bat_cut_c < s->temp_bat_safe_c + 1.0f) {
        s->temp_bat_cut_c = s->temp_bat_safe_c + 1.0f;
    }

    if (!_finitef(s->temp_inv_warn_c)) s->temp_inv_warn_c = def.temp_inv_warn_c;
    if (!_finitef(s->temp_inv_safe_c)) s->temp_inv_safe_c = def.temp_inv_safe_c;
    if (!_finitef(s->temp_inv_cut_c)) s->temp_inv_cut_c = def.temp_inv_cut_c;
    s->temp_inv_warn_c = _clampf(s->temp_inv_warn_c, 20.0f, 95.0f);
    s->temp_inv_safe_c = _clampf(s->temp_inv_safe_c, 25.0f, 100.0f);
    s->temp_inv_cut_c = _clampf(s->temp_inv_cut_c, 30.0f, 105.0f);
    if (s->temp_inv_safe_c < s->temp_inv_warn_c + 1.0f) {
        s->temp_inv_safe_c = s->temp_inv_warn_c + 1.0f;
    }
    if (s->temp_inv_cut_c < s->temp_inv_safe_c + 1.0f) {
        s->temp_inv_cut_c = s->temp_inv_safe_c + 1.0f;
    }

    if (!_finitef(s->shunt_dis_mohm)) s->shunt_dis_mohm = def.shunt_dis_mohm;
    if (!_finitef(s->shunt_chg_mohm)) s->shunt_chg_mohm = def.shunt_chg_mohm;
    s->shunt_dis_mohm = _clampf(s->shunt_dis_mohm, 0.10f, 5.0f);
    s->shunt_chg_mohm = _clampf(s->shunt_chg_mohm, 0.10f, 5.0f);

    if (!_finitef(s->pack_dis_v_gain)) s->pack_dis_v_gain = def.pack_dis_v_gain;
    if (!_finitef(s->pack_chg_v_gain)) s->pack_chg_v_gain = def.pack_chg_v_gain;
    if (!_finitef(s->cell1_v_gain)) s->cell1_v_gain = def.cell1_v_gain;
    if (!_finitef(s->cell2_v_gain)) s->cell2_v_gain = def.cell2_v_gain;
    if (!_finitef(s->cell3_v_gain)) s->cell3_v_gain = def.cell3_v_gain;
    s->pack_dis_v_gain = _clampf(s->pack_dis_v_gain, 0.80f, 1.20f);
    s->pack_chg_v_gain = _clampf(s->pack_chg_v_gain, 0.80f, 1.20f);
    s->cell1_v_gain = _clampf(s->cell1_v_gain, 0.80f, 1.20f);
    s->cell2_v_gain = _clampf(s->cell2_v_gain, 0.80f, 1.20f);
    s->cell3_v_gain = _clampf(s->cell3_v_gain, 0.80f, 1.20f);

    if (!_finitef(s->cal_ref_dis_current_a)) s->cal_ref_dis_current_a = def.cal_ref_dis_current_a;
    if (!_finitef(s->cal_ref_dis_voltage_v)) s->cal_ref_dis_voltage_v = def.cal_ref_dis_voltage_v;
    if (!_finitef(s->cal_ref_chg_current_a)) s->cal_ref_chg_current_a = def.cal_ref_chg_current_a;
    if (!_finitef(s->cal_ref_chg_voltage_v)) s->cal_ref_chg_voltage_v = def.cal_ref_chg_voltage_v;
    if (!_finitef(s->cal_ref_cell1_v)) s->cal_ref_cell1_v = def.cal_ref_cell1_v;
    if (!_finitef(s->cal_ref_cell2_v)) s->cal_ref_cell2_v = def.cal_ref_cell2_v;
    if (!_finitef(s->cal_ref_cell3_v)) s->cal_ref_cell3_v = def.cal_ref_cell3_v;
    s->cal_ref_dis_current_a = _clampf(s->cal_ref_dis_current_a, 0.0f, IMAX_DIS_A * 1.5f);
    s->cal_ref_dis_voltage_v = _clampf(s->cal_ref_dis_voltage_v, 0.0f, 20.0f);
    s->cal_ref_chg_current_a = _clampf(s->cal_ref_chg_current_a, 0.0f, IMAX_CHG_A * 1.5f);
    s->cal_ref_chg_voltage_v = _clampf(s->cal_ref_chg_voltage_v, 0.0f, 20.0f);
    s->cal_ref_cell1_v = _clampf(s->cal_ref_cell1_v, 0.0f, 5.0f);
    s->cal_ref_cell2_v = _clampf(s->cal_ref_cell2_v, 0.0f, 5.0f);
    s->cal_ref_cell3_v = _clampf(s->cal_ref_cell3_v, 0.0f, 5.0f);

    s->ui_brightness = (uint8_t)_clampf((float)s->ui_brightness, 10.0f, 100.0f);
    s->buzzer_en = s->buzzer_en ? 1u : 0u;
}

void settings_init(void) {
    SystemSettings loaded;
    SystemSettingsV1 legacy;
    uint32_t seq = 0;
    uint8_t slot = 0;
    bool loaded_ok = false;
    bool migrated_v1 = false;

    if (nvm_ab_load(FLASH_SETTINGS_OFFSET, FLASH_SETTINGS_OFFSET_B,
                    SETTINGS_MAGIC,
                    &loaded, sizeof(loaded),
                    &seq, &slot) &&
        loaded.version == SETTINGS_VERSION) {
        loaded_ok = true;
    } else if (nvm_ab_load(FLASH_SETTINGS_OFFSET, FLASH_SETTINGS_OFFSET_B,
                           SETTINGS_MAGIC,
                           &legacy, sizeof(legacy),
                           &seq, &slot) &&
               legacy.version == 1u) {
        _settings_from_v1(&loaded, &legacy);
        loaded_ok = true;
        migrated_v1 = true;
    }

    if (loaded_ok) {
        _settings_sanitize(&loaded);
        g_settings = loaded;
        g_settings_seq = seq;
        g_settings_slot = slot;
        printf("[SET] %s: cap=%.1fAh chg_shunt=%.3fmOhm dis_gain=%.4f\n",
               migrated_v1 ? "migrated v1" : "loaded",
               g_settings.capacity_ah,
               g_settings.shunt_chg_mohm,
               g_settings.pack_dis_v_gain);
    } else {
        settings_defaults(&g_settings);
        g_settings_seq = 0;
        g_settings_slot = 0;
        printf("[SET] defaults\n");
    }
    g_settings_ready = true;
}

void settings_copy(SystemSettings *out) {
    if (!out) return;
    if (!g_settings_ready) settings_init();
    *out = g_settings;
}

const SystemSettings *settings_get(void) {
    if (!g_settings_ready) settings_init();
    return &g_settings;
}

bool settings_store(const SystemSettings *next) {
    SystemSettings copy;

    if (!next) return false;
    copy = *next;
    _settings_sanitize(&copy);

    if (!nvm_ab_save(FLASH_SETTINGS_OFFSET, FLASH_SETTINGS_OFFSET_B,
                     SETTINGS_MAGIC,
                     &g_settings_seq, &g_settings_slot,
                     &copy, sizeof(copy))) {
        printf("[SET] save FAILED\n");
        return false;
    }

    g_settings = copy;
    g_settings_ready = true;
    printf("[SET] saved: cap=%.1fAh cut=%.2fV/%.2fV chg_shunt=%.3f\n",
           g_settings.capacity_ah,
           g_settings.vbat_cut_v,
           g_settings.cell_cut_v,
           g_settings.shunt_chg_mohm);
    return true;
}

bool settings_reset_defaults(void) {
    SystemSettings def;
    settings_defaults(&def);
    return settings_store(&def);
}
