// ============================================================
// app/buzzer.c - Non-blocking passive buzzer driver
// ============================================================
#include "buzzer.h"
#include "config.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define BUZ_SYS_CLK_HZ     125000000u
#define BUZ_PWM_CLKDIV_INT 16u
#define BUZ_DEFAULT_HZ     988u
#define BUZ_MIN_HZ         120u
#define BUZ_MAX_HZ         8000u

typedef struct {
    const uint16_t *steps_ms;
    const uint16_t *tones_hz;
    uint16_t base_hz;
} BuzPatternDef;

static uint8_t _buz_preset_duty(BuzzerPreset preset) {
    switch (preset) {
        case BUZ_PRESET_MINIMAL: return 16u;
        case BUZ_PRESET_SILENT:  return 0u;
        case BUZ_PRESET_FULL:
        default:                 return 34u;
    }
}

static bool _buz_preset_allows(BuzzerPreset preset, BuzPattern p) {
    if (preset == BUZ_PRESET_SILENT) return false;
    if (preset == BUZ_PRESET_FULL) return true;

    switch (p) {
        case BUZ_BOOT:
        case BUZ_SOC_CUT:
        case BUZ_TEMP_CUT:
        case BUZ_OCP:
        case BUZ_CHARGE_DONE:
        case BUZ_CHARGE_START:
        case BUZ_KEY_CLICK:
        case BUZ_KEY_LONG:
        case BUZ_ALARM_CRIT:
        case BUZ_ALARM_WARN:
        case BUZ_POWEROFF_READY:
        case BUZ_POWEROFF_COUNT_3:
        case BUZ_POWEROFF_COUNT_2:
        case BUZ_POWEROFF_COUNT_1:
            return true;
        case BUZ_SOC_WARN:
        case BUZ_TEMP_WARN:
        default:
            return false;
    }
}

const char *buz_preset_name(BuzzerPreset preset) {
    switch (preset) {
        case BUZ_PRESET_MINIMAL: return "MINIMAL";
        case BUZ_PRESET_SILENT:  return "SILENT";
        case BUZ_PRESET_FULL:
        default:                 return "FULL";
    }
}

static uint16_t _scale_hz(uint16_t hz, BuzzerPreset preset) {
    if (hz < BUZ_MIN_HZ) hz = BUZ_MIN_HZ;
    if (hz > BUZ_MAX_HZ) hz = BUZ_MAX_HZ;
    if (preset == BUZ_PRESET_MINIMAL) {
        hz = (uint16_t)((hz * 92u) / 100u);
    }
    if (hz < BUZ_MIN_HZ) hz = BUZ_MIN_HZ;
    return hz;
}

static void _buz_drive_low(Buzzer *bz) {
    uint slice = pwm_gpio_to_slice_num(bz->gpio);
    pwm_set_enabled(slice, false);
    gpio_set_function(bz->gpio, GPIO_FUNC_SIO);
    gpio_set_dir(bz->gpio, GPIO_OUT);
    gpio_put(bz->gpio, 0);
}

static void _buz_drive_pwm(Buzzer *bz, uint16_t hz) {
    uint slice = pwm_gpio_to_slice_num(bz->gpio);
    uint chan = pwm_gpio_to_channel(bz->gpio);
    uint32_t top;
    uint32_t level;

    if (bz->duty_pct == 0u) {
        _buz_drive_low(bz);
        return;
    }

    if (hz < BUZ_MIN_HZ) hz = BUZ_MIN_HZ;
    if (hz > BUZ_MAX_HZ) hz = BUZ_MAX_HZ;

    gpio_set_function(bz->gpio, GPIO_FUNC_PWM);
    pwm_set_clkdiv_int_frac(slice, BUZ_PWM_CLKDIV_INT, 0);
    top = BUZ_SYS_CLK_HZ / (BUZ_PWM_CLKDIV_INT * (uint32_t)hz);
    if (top < 2u) top = 2u;
    if (top > 65535u) top = 65535u;

    level = (top * bz->duty_pct) / 100u;
    if (level == 0u) level = 1u;
    if (level >= top) level = top - 1u;

    pwm_set_wrap(slice, (uint16_t)(top - 1u));
    pwm_set_chan_level(slice, chan, (uint16_t)level);
    pwm_set_counter(slice, 0u);
    pwm_set_enabled(slice, true);
}

static void _buz_pwm_set(Buzzer *bz, bool on, uint16_t hz) {
    if (on && bz->enabled && bz->duty_pct > 0u) _buz_drive_pwm(bz, hz);
    else _buz_drive_low(bz);
}

static void _buz_reset_state(Buzzer *bz) {
    bz->pattern = NULL;
    bz->pat_idx = 0;
    bz->pat_end_ms = 0;
    bz->pat_on = false;
    bz->active_pattern = BUZ_COUNT;
}

static const uint16_t _PAT_BOOT[]       = {48, 14, 54, 14, 62, 14, 84, 0};
static const uint16_t _PAT_SOC_WARN[]   = {90, 80, 120, 420, 0};
static const uint16_t _PAT_SOC_CUT[]    = {80, 55, 80, 55, 120, 260, 0};
static const uint16_t _PAT_TEMP_WARN[]  = {90, 70, 120, 500, 0};
static const uint16_t _PAT_TEMP_CUT[]   = {70, 45, 70, 45, 70, 220, 0};
static const uint16_t _PAT_OCP[]        = {50, 40, 50, 40, 80, 240, 0};
static const uint16_t _PAT_CHG_DONE[]   = {70, 55, 90, 55, 130, 0};
static const uint16_t _PAT_CHG_START[]  = {60, 40, 90, 0};
static const uint16_t _PAT_CLICK[]      = {18, 0};
static const uint16_t _PAT_LONG[]       = {40, 0};
static const uint16_t _PAT_ALARM_CRIT[] = {45, 35, 45, 35, 70, 220, 0};
static const uint16_t _PAT_ALARM_WARN[] = {80, 60, 100, 420, 0};
static const uint16_t _PAT_POWEROFF_READY[]   = {45, 40, 60, 0};
static const uint16_t _PAT_POWEROFF_COUNT_3[] = {36, 10, 48, 0};
static const uint16_t _PAT_POWEROFF_COUNT_2[] = {36, 10, 48, 0};
static const uint16_t _PAT_POWEROFF_COUNT_1[] = {36, 10, 54, 0};

static const uint16_t _TONES_BOOT[]      = {784, 988, 1175, 1568, 0};
static const uint16_t _TONES_SOC_WARN[]  = {740, 659, 0};
static const uint16_t _TONES_SOC_CUT[]   = {659, 587, 523, 0};
static const uint16_t _TONES_TEMP_WARN[] = {988, 880, 0};
static const uint16_t _TONES_TEMP_CUT[]  = {1319, 1175, 1046, 0};
static const uint16_t _TONES_OCP[]       = {1480, 1245, 988, 0};
static const uint16_t _TONES_CHG_DONE[]  = {784, 988, 1319, 0};
static const uint16_t _TONES_CHG_START[] = {659, 880, 0};
static const uint16_t _TONES_ALARM_CRIT[] = {1568, 1175, 784, 0};
static const uint16_t _TONES_ALARM_WARN[] = {1046, 880, 0};
static const uint16_t _TONES_POWEROFF_READY[]   = {1175, 880, 0};
static const uint16_t _TONES_POWEROFF_COUNT_3[] = {1568, 1319, 0};
static const uint16_t _TONES_POWEROFF_COUNT_2[] = {1319, 1046, 0};
static const uint16_t _TONES_POWEROFF_COUNT_1[] = {1046, 784, 0};

static const BuzPatternDef _PATTERNS[BUZ_COUNT] = {
    { _PAT_BOOT,       _TONES_BOOT,       784  },
    { _PAT_SOC_WARN,   _TONES_SOC_WARN,   740  },
    { _PAT_SOC_CUT,    _TONES_SOC_CUT,    659  },
    { _PAT_TEMP_WARN,  _TONES_TEMP_WARN,  988  },
    { _PAT_TEMP_CUT,   _TONES_TEMP_CUT,   1319 },
    { _PAT_OCP,        _TONES_OCP,        1480 },
    { _PAT_CHG_DONE,   _TONES_CHG_DONE,   784  },
    { _PAT_CHG_START,  _TONES_CHG_START,  659  },
    { _PAT_CLICK,      NULL,              880  },
    { _PAT_LONG,       NULL,              740  },
    { _PAT_ALARM_CRIT, _TONES_ALARM_CRIT, 1568 },
    { _PAT_ALARM_WARN, _TONES_ALARM_WARN, 1046 },
    { _PAT_POWEROFF_READY,   _TONES_POWEROFF_READY,   1175 },
    { _PAT_POWEROFF_COUNT_3, _TONES_POWEROFF_COUNT_3, 1568 },
    { _PAT_POWEROFF_COUNT_2, _TONES_POWEROFF_COUNT_2, 1319 },
    { _PAT_POWEROFF_COUNT_1, _TONES_POWEROFF_COUNT_1, 1046 },
};

static uint16_t _buz_pattern_hz(const Buzzer *bz, BuzPattern p, uint8_t pat_idx) {
    const BuzPatternDef *def;
    const uint16_t *tones;
    uint8_t on_idx;
    uint16_t hz;

    if (p >= BUZ_COUNT) return BUZ_DEFAULT_HZ;
    def = &_PATTERNS[p];
    tones = def->tones_hz;
    on_idx = (uint8_t)(pat_idx >> 1);

    if (tones && tones[on_idx] != 0u) hz = tones[on_idx];
    else if (def->base_hz != 0u)      hz = def->base_hz;
    else                              hz = BUZ_DEFAULT_HZ;

    return _scale_hz(hz, (BuzzerPreset)bz->preset);
}

static void _buz_service_locked(Buzzer *bz, uint32_t now_ms) {
    while (bz->pattern && (int32_t)(now_ms - bz->pat_end_ms) >= 0) {
        bz->pat_idx++;

        if (bz->pattern[bz->pat_idx] == 0u) {
            _buz_pwm_set(bz, false, 0);
            _buz_reset_state(bz);
            return;
        }

        bz->pat_on = !bz->pat_on;
        if (bz->pat_on) {
            _buz_pwm_set(bz, true,
                         _buz_pattern_hz(bz, (BuzPattern)bz->active_pattern, bz->pat_idx));
        } else {
            _buz_pwm_set(bz, false, 0);
        }
        bz->pat_end_ms += bz->pattern[bz->pat_idx];
    }
}

static bool _buz_timer_cb(repeating_timer_t *rt) {
    Buzzer *bz = (Buzzer *)rt->user_data;
    critical_section_enter_blocking(&bz->lock);
    _buz_service_locked(bz, to_ms_since_boot(get_absolute_time()));
    critical_section_exit(&bz->lock);
    return true;
}

void buz_init(Buzzer *bz, uint8_t gpio, bool enabled) {
    memset(bz, 0, sizeof(*bz));
    bz->gpio = gpio;
    bz->enabled = enabled;
    bz->preset = BUZ_PRESET_FULL;
    bz->duty_pct = enabled ? _buz_preset_duty(BUZ_PRESET_FULL) : 0u;
    bz->active_pattern = BUZ_COUNT;

    critical_section_init(&bz->lock);
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_OUT);
    gpio_put(gpio, 0);
    _buz_drive_low(bz);

    if (!bz->timer_started) {
        bz->timer_started = add_repeating_timer_ms(-5, _buz_timer_cb, bz, &bz->timer);
        if (!bz->timer_started) {
            printf("[BUZ] timer start FAILED, using buz_tick fallback\n");
        }
    }
}

void buz_set_enabled(Buzzer *bz, bool en) {
    critical_section_enter_blocking(&bz->lock);
    bz->enabled = en;
    bz->duty_pct = en ? _buz_preset_duty((BuzzerPreset)bz->preset) : 0u;
    if (!en || bz->duty_pct == 0u) {
        _buz_pwm_set(bz, false, 0);
        _buz_reset_state(bz);
    }
    critical_section_exit(&bz->lock);
}

void buz_set_preset(Buzzer *bz, BuzzerPreset preset) {
    if (preset >= BUZ_PRESET_COUNT) preset = BUZ_PRESET_FULL;

    critical_section_enter_blocking(&bz->lock);
    bz->preset = (uint8_t)preset;
    bz->duty_pct = bz->enabled ? _buz_preset_duty(preset) : 0u;

    if (!bz->enabled || bz->duty_pct == 0u) {
        _buz_pwm_set(bz, false, 0);
        _buz_reset_state(bz);
    } else if (bz->pattern && bz->pat_on) {
        _buz_pwm_set(bz, true,
                     _buz_pattern_hz(bz, (BuzPattern)bz->active_pattern, bz->pat_idx));
    }
    critical_section_exit(&bz->lock);
}

BuzzerPreset buz_get_preset(const Buzzer *bz) {
    if (!bz) return BUZ_PRESET_FULL;
    return (BuzzerPreset)bz->preset;
}

void buz_play(Buzzer *bz, BuzPattern p) {
    uint32_t now_ms;
    const BuzPatternDef *def;
    BuzzerPreset preset;

    if (p >= BUZ_COUNT) return;

    critical_section_enter_blocking(&bz->lock);
    preset = (BuzzerPreset)bz->preset;
    if (!bz->enabled || !_buz_preset_allows(preset, p) || bz->duty_pct == 0u) {
        critical_section_exit(&bz->lock);
        return;
    }

    now_ms = to_ms_since_boot(get_absolute_time());
    if (bz->pattern &&
        bz->active_pattern == (uint8_t)p &&
        (int32_t)(now_ms - bz->last_start_ms) < 40) {
        critical_section_exit(&bz->lock);
        return;
    }

    def = &_PATTERNS[p];
    bz->pattern = def->steps_ms;
    bz->pat_idx = 0;
    bz->pat_on = true;
    bz->active_pattern = (uint8_t)p;
    bz->last_start_ms = now_ms;
    bz->pat_end_ms = now_ms + bz->pattern[0];
    _buz_pwm_set(bz, true, _buz_pattern_hz(bz, p, 0));
    critical_section_exit(&bz->lock);
}

void buz_tick(Buzzer *bz) {
    critical_section_enter_blocking(&bz->lock);
    _buz_service_locked(bz, to_ms_since_boot(get_absolute_time()));
    critical_section_exit(&bz->lock);
}
