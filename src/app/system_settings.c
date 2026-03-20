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

static float _clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool _finitef(float v) {
    return !isnan(v) && !isinf(v);
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

    s->ui_brightness = (uint8_t)_clampf((float)s->ui_brightness, 10.0f, 100.0f);
    s->buzzer_en = s->buzzer_en ? 1u : 0u;
}

void settings_init(void) {
    SystemSettings loaded;
    uint32_t seq = 0;
    uint8_t slot = 0;

    if (nvm_ab_load(FLASH_SETTINGS_OFFSET, FLASH_SETTINGS_OFFSET_B,
                    SETTINGS_MAGIC,
                    &loaded, sizeof(loaded),
                    &seq, &slot) &&
        loaded.version == SETTINGS_VERSION) {
        _settings_sanitize(&loaded);
        g_settings = loaded;
        g_settings_seq = seq;
        g_settings_slot = slot;
        printf("[SET] loaded: cap=%.1fAh chg_shunt=%.3fmOhm\n",
               g_settings.capacity_ah, g_settings.shunt_chg_mohm);
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
