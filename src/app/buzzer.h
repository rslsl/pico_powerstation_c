#pragma once
// ============================================================
// app/buzzer.h — Non-blocking Buzzer Driver (active piezo)
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include "pico/time.h"
#include "pico/sync.h"

typedef enum {
    BUZ_BOOT = 0, BUZ_SOC_WARN, BUZ_SOC_CUT,
    BUZ_TEMP_WARN, BUZ_TEMP_CUT, BUZ_OCP,
    BUZ_CHARGE_DONE, BUZ_CHARGE_START,
    BUZ_KEY_CLICK, BUZ_KEY_LONG,
    BUZ_ALARM_CRIT, BUZ_ALARM_WARN,
    BUZ_POWEROFF_READY,
    BUZ_POWEROFF_COUNT_3,
    BUZ_POWEROFF_COUNT_2,
    BUZ_POWEROFF_COUNT_1,
    BUZ_COUNT
} BuzPattern;

typedef enum {
    BUZ_PRESET_FULL = 0,
    BUZ_PRESET_MINIMAL,
    BUZ_PRESET_SILENT,
    BUZ_PRESET_COUNT
} BuzzerPreset;

typedef struct {
    bool     enabled;
    uint8_t  gpio;
    uint8_t  preset;
    uint8_t  duty_pct;
    // Поточний патерн
    const uint16_t *pattern;   // ms on/off пари, 0=кінець
    uint8_t  pat_idx;
    uint32_t pat_end_ms;       // коли переключити
    bool     pat_on;
    uint32_t last_start_ms;
    uint8_t  active_pattern;
    critical_section_t lock;
    repeating_timer_t  timer;
    bool     timer_started;
} Buzzer;

void buz_init(Buzzer *bz, uint8_t gpio, bool enabled);
void buz_set_enabled(Buzzer *bz, bool en);
void buz_set_preset(Buzzer *bz, BuzzerPreset preset);
BuzzerPreset buz_get_preset(const Buzzer *bz);
const char *buz_preset_name(BuzzerPreset preset);
void buz_play(Buzzer *bz, BuzPattern p);
void buz_tick(Buzzer *bz);    // викликати кожні ~10ms
