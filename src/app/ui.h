#pragma once
// ============================================================
// app/ui.h - UI State Machine
// ============================================================
#include <stdbool.h>
#include <stdint.h>

#include "hardware/sync.h"
#include "display.h"
#include "power_control.h"
#include "power_sequencer.h"
#include "buzzer.h"
#include "protection.h"
#include "system_settings.h"
#include "../bms/battery.h"
#include "../bms/bms_stats.h"
#include "../bms/bms_logger.h"

typedef enum {
    S_MAIN = 0,
    S_STATS = 1,
    S_BATTERY = 2,
    S_DIAGNOSTICS = 3,
    S_PORTS = 4,
    S_UI_CFG = 5,
    S_BAT_CFG = 6,
    S_TEMP_CFG = 7,
    S_CAL_CFG = 8,
    S_CAL_ITEM = 9,
    S_CAL_EDIT = 10,
    S_DATA_CFG = 11,
    S_ADVANCED = 12,
    S_EVENTS = 13,
    S_HISTORY = 14,
    S_I2C_SCAN = 15,
    S_COUNT
} UiState;

typedef enum {
    UI_CONFIRM_NONE = 0,
    UI_CONFIRM_TOGGLE_PORT = 1,
    UI_CONFIRM_RESET_LATCH = 2,
    UI_CONFIRM_CLEAR_LOG = 3,
    UI_CONFIRM_RESET_STATS = 4,
    UI_CONFIRM_RESET_STATS_FINAL = 5,
    UI_CONFIRM_APPLY_CALIB = 6,
} UiConfirmAction;

typedef void (*UiSettingsApplyFn)(bool reconfigure_sensors);

typedef struct {
    BatSnapshot bat;

    uint32_t alarms;
    uint32_t latched_faults;

    bool port_on[PORT_COUNT];

    bool safe_mode;
    bool startup_ok;
    bool charger_present;
    uint8_t meas_valid;

    float efc_total;
    float energy_in_wh;
    float energy_out_wh;
    float soh_last;
    float temp_min_c;
    float temp_max_c;
    float temp_avg_c;
    float peak_current_a;
    float peak_power_w;
    float session_h;
    float session_peak_a;
    float session_peak_w;
    uint32_t boot_count;
    uint32_t total_alarm_events;
    uint32_t total_ocp_events;
    uint32_t total_temp_events;

    uint32_t log_total;
    struct {
        uint32_t timestamp_s;
        uint8_t type;
        uint8_t soc_pct;
        uint8_t temp_bat;
        float voltage;
        float current;
        float param;
        uint32_t alarm_flags;
    } log_cache[8];
    uint8_t log_cache_n;

    uint8_t scan_found[8][16];
    uint8_t scan_counts[8];
    bool scan_valid;
    bool confirm_active;
    uint8_t confirm_kind;
    int8_t confirm_arg;
    SystemSettings settings;
    bool edit_active;
    uint8_t cal_sensor;
    uint8_t cal_target;
    float cal_ref_value;
    float cal_measured_avg;
    uint8_t cal_measured_n;

    UiState state;
    int8_t cur[S_COUNT];
    int16_t ev_scroll;
    int8_t hist_page;
    float soc_anim;
    bool blink;
    bool screensaver_active;
    uint16_t anim_phase;
    uint32_t idle_ms;
    char toast_msg[48];
    uint32_t toast_end_ms;
    bool poweroff_pending;
    uint32_t poweroff_remaining_ms;
    uint8_t brightness;
    bool buzzer_en;
} FullUiSnapshot;

typedef struct {
    Display *disp;
    PowerControl *pwr;
    PowerSeq *pseq;
    Buzzer *buz;
    BatSnapshot *snap;
    Protection *prot;
    BmsStats *stats;
    BmsLogger *logger;

    UiState state;
    bool dirty;

    volatile int8_t nav_delta;
    bool ok_btn_held;
    uint32_t ok_btn_press_ms;
    uint32_t btn_last_ms[3];
    bool btn_prev[3];

    int8_t cur[S_COUNT];
    int16_t ev_scroll;
    int8_t hist_page;

    char toast_msg[48];
    uint32_t toast_end_ms;

    uint8_t scan_found[8][16];
    uint8_t scan_counts[8];
    bool scan_valid;
    bool confirm_active;
    uint8_t confirm_kind;
    int8_t confirm_arg;
    bool edit_active;
    uint8_t cal_sensor;
    uint8_t cal_target;
    float cal_ref_value;
    float cal_measured_buf[10];
    int   cal_measured_idx;
    int   cal_measured_n;
    float cal_measured_avg;
    uint32_t cal_measured_last_ms;

    float soc_anim;
    bool blink;
    uint32_t blink_ms;
    uint32_t anim_phase_ms;
    uint16_t anim_phase;
    uint32_t last_activity_ms;
    bool screensaver_active;

    uint8_t brightness;
    bool buzzer_en;
    UiSettingsApplyFn apply_settings;

    bool startup_ok;

    bool poweroff_armed;
    uint32_t poweroff_arm_ms;
    bool poweroff_audio_started;
    uint8_t poweroff_audio_stage;

    FullUiSnapshot rs;
    spin_lock_t *render_lock;
    uint render_lock_num;
} UI;

void ui_init(UI *ui, Display *d, PowerControl *pwr, PowerSeq *pseq,
             Buzzer *buz, BatSnapshot *snap, Protection *prot,
             BmsStats *stats, BmsLogger *logger,
             UiSettingsApplyFn apply_settings);

void ui_poll(UI *ui);
void ui_render(UI *ui);

void ui_toast(UI *ui, const char *msg);
void ui_set_startup_ok(UI *ui, bool ok);
void ui_set_state(UI *ui, UiState s);
void ui_refresh(UI *ui);
