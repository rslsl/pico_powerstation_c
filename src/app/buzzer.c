// ============================================================
// app/buzzer.c - Non-blocking passive buzzer driver
// ============================================================
#include "buzzer.h"
#include "config.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include <string.h>

#define BUZ_SYS_CLK_HZ     125000000u
#define BUZ_PWM_CLKDIV_INT 16u
#define BUZ_DEFAULT_HZ     1046u
#define BUZ_MIN_HZ         120u
#define BUZ_MAX_HZ         8000u

typedef struct {
    const uint16_t *steps_ms;
    const uint16_t *tones_hz;
    uint16_t base_hz;
} BuzPatternDef;

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

    if (hz < BUZ_MIN_HZ) hz = BUZ_MIN_HZ;
    if (hz > BUZ_MAX_HZ) hz = BUZ_MAX_HZ;

    gpio_set_function(bz->gpio, GPIO_FUNC_PWM);
    pwm_set_clkdiv_int_frac(slice, BUZ_PWM_CLKDIV_INT, 0);
    top = BUZ_SYS_CLK_HZ / (BUZ_PWM_CLKDIV_INT * (uint32_t)hz);
    if (top < 2u) top = 2u;
    if (top > 65535u) top = 65535u;

    pwm_set_wrap(slice, (uint16_t)(top - 1u));
    pwm_set_chan_level(slice, chan, (uint16_t)(top / 2u));
    pwm_set_counter(slice, 0u);
    pwm_set_enabled(slice, true);
}

static void _buz_pwm_set(Buzzer *bz, bool on, uint16_t hz) {
    if (on) _buz_drive_pwm(bz, hz);
    else _buz_drive_low(bz);
}

static void _buz_reset_state(Buzzer *bz) {
    bz->pattern = NULL;
    bz->pat_idx = 0;
    bz->pat_end_ms = 0;
    bz->pat_on = false;
    bz->active_pattern = BUZ_COUNT;
}

static const uint16_t _PAT_BOOT[]       = {70, 30, 70, 30, 110, 0};
static const uint16_t _PAT_SOC_WARN[]   = {200, 200, 200, 600, 0};
static const uint16_t _PAT_SOC_CUT[]    = {100, 80, 100, 80, 100, 80, 100, 600, 0};
static const uint16_t _PAT_TEMP_WARN[]  = {150, 100, 150, 600, 0};
static const uint16_t _PAT_TEMP_CUT[]   = {80, 60, 80, 60, 80, 60, 80, 60, 80, 800, 0};
static const uint16_t _PAT_OCP[]        = {60, 40, 60, 40, 60, 40, 60, 800, 0};
static const uint16_t _PAT_CHG_DONE[]   = {100, 80, 120, 80, 180, 0};
static const uint16_t _PAT_CHG_START[]  = {80, 80, 80, 0};
static const uint16_t _PAT_CLICK[]      = {30, 0};
static const uint16_t _PAT_LONG[]       = {60, 0};
static const uint16_t _PAT_ALARM_CRIT[] = {50, 40, 50, 40, 50, 40, 50, 40, 50, 600, 0};
static const uint16_t _PAT_ALARM_WARN[] = {120, 120, 120, 500, 0};
static const uint16_t _PAT_SHUTDOWN[]   = {2000, 0};

static const uint16_t _TONES_BOOT[]      = {1319, 1568, 1976, 0};
static const uint16_t _TONES_CHG_DONE[]  = {1175, 1397, 1760, 0};
static const uint16_t _TONES_CHG_START[] = {1046, 1397, 0};

static const BuzPatternDef _PATTERNS[BUZ_COUNT] = {
    { _PAT_BOOT,       _TONES_BOOT,      1319 },
    { _PAT_SOC_WARN,   NULL,             1046 },
    { _PAT_SOC_CUT,    NULL,              988 },
    { _PAT_TEMP_WARN,  NULL,             1175 },
    { _PAT_TEMP_CUT,   NULL,             1568 },
    { _PAT_OCP,        NULL,             1760 },
    { _PAT_CHG_DONE,   _TONES_CHG_DONE,  1175 },
    { _PAT_CHG_START,  _TONES_CHG_START, 1046 },
    { _PAT_CLICK,      NULL,             1397 },
    { _PAT_LONG,       NULL,             1175 },
    { _PAT_ALARM_CRIT, NULL,             1865 },
    { _PAT_ALARM_WARN, NULL,             1480 },
    { _PAT_SHUTDOWN,   NULL,              440 },
};

static uint16_t _buz_pattern_hz(BuzPattern p, uint8_t pat_idx) {
    const BuzPatternDef *def;
    const uint16_t *tones;
    uint8_t on_idx;

    if (p >= BUZ_COUNT) return BUZ_DEFAULT_HZ;
    def = &_PATTERNS[p];
    tones = def->tones_hz;
    on_idx = (uint8_t)(pat_idx >> 1);

    if (tones && tones[on_idx] != 0) return tones[on_idx];
    if (def->base_hz != 0) return def->base_hz;
    return BUZ_DEFAULT_HZ;
}

static void _buz_service_locked(Buzzer *bz, uint32_t now_ms) {
    while (bz->pattern && (int32_t)(now_ms - bz->pat_end_ms) >= 0) {
        bz->pat_idx++;

        if (bz->pattern[bz->pat_idx] == 0) {
            _buz_pwm_set(bz, false, 0);
            _buz_reset_state(bz);
            return;
        }

        bz->pat_on = !bz->pat_on;
        if (bz->pat_on) {
            _buz_pwm_set(bz, true, _buz_pattern_hz((BuzPattern)bz->active_pattern, bz->pat_idx));
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
    bz->active_pattern = BUZ_COUNT;

    critical_section_init(&bz->lock);
    gpio_init(gpio);
    gpio_set_dir(gpio, GPIO_OUT);
    gpio_put(gpio, 0);
    _buz_drive_low(bz);

    if (!bz->timer_started) {
        add_repeating_timer_ms(-5, _buz_timer_cb, bz, &bz->timer);
        bz->timer_started = true;
    }
}

void buz_set_enabled(Buzzer *bz, bool en) {
    critical_section_enter_blocking(&bz->lock);
    bz->enabled = en;
    if (!en) {
        _buz_pwm_set(bz, false, 0);
        _buz_reset_state(bz);
    }
    critical_section_exit(&bz->lock);
}

void buz_play(Buzzer *bz, BuzPattern p) {
    uint32_t now_ms;
    const BuzPatternDef *def;

    if (p >= BUZ_COUNT) return;

    critical_section_enter_blocking(&bz->lock);
    if (!bz->enabled) {
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
    _buz_pwm_set(bz, true, _buz_pattern_hz(p, 0));
    critical_section_exit(&bz->lock);
}

void buz_tick(Buzzer *bz) {
    critical_section_enter_blocking(&bz->lock);
    _buz_service_locked(bz, to_ms_since_boot(get_absolute_time()));
    critical_section_exit(&bz->lock);
}
