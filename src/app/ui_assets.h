#pragma once

#include <stdint.h>

typedef struct {
    uint8_t w;
    uint8_t h;
    const uint8_t *bits;
} UiIcon;

extern const UiIcon UI_ICON_DC;
extern const UiIcon UI_ICON_PD;
extern const UiIcon UI_ICON_FAN;
extern const UiIcon UI_ICON_CHARGE;
extern const UiIcon UI_ICON_BATTERY;
extern const UiIcon UI_ICON_CELLS;
extern const UiIcon UI_ICON_CLOCK;
extern const UiIcon UI_ICON_CHARGING;
extern const UiIcon UI_ICON_DISCHARGING;
extern const UiIcon UI_ICON_STANDBY;
extern const UiIcon UI_ICON_THERMO;
extern const UiIcon UI_ICON_POWER;
extern const UiIcon UI_ICON_SHIELD;
extern const UiIcon UI_ICON_LOCK;
extern const UiIcon UI_ICON_INFO;
extern const UiIcon UI_ICON_SETTINGS;
extern const UiIcon UI_ICON_LOGS;
extern const UiIcon UI_ICON_SUN;
extern const UiIcon UI_ICON_WARN;
extern const UiIcon UI_ICON_BT;
extern const UiIcon UI_ICON_SLEEP;
extern const UiIcon UI_ICON_BELL;
extern const UiIcon UI_ICON_WIFI;
extern const UiIcon UI_ICON_PULSE;
