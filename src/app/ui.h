#pragma once
// ============================================================
// app/ui.h — UI State Machine
//
// v4.0 fixes:
//   P0.3  — BatSnapshot embedded in FullUiSnapshot; Core1 NEVER
//            reads ui->snap or g_bat_snapshot directly
//   P1.7  — alarms, latched_faults, port states, stats summary,
//            log cache — all in FullUiSnapshot; no live structs
//            accessed from Core1 render path
//   P3.12 — splash text fixed: "Reboot after fixing fault"
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include "hardware/sync.h"
#include "display.h"
#include "power_control.h"
#include "power_sequencer.h"
#include "buzzer.h"
#include "protection.h"
#include "../bms/battery.h"
#include "../bms/bms_stats.h"
#include "../bms/bms_logger.h"

typedef enum {
    S_MAIN     = 0,
    S_STATS    = 1,
    S_BATTERY  = 2,
    S_DIAGNOSTICS = 3,
    S_PORTS    = 4,
    S_UI_CFG   = 5,
    S_ADVANCED = 6,
    S_EVENTS   = 7,
    S_HISTORY  = 8,
    S_I2C_SCAN = 9,
    S_COUNT
} UiState;

typedef enum {
    UI_CONFIRM_NONE = 0,
    UI_CONFIRM_TOGGLE_PORT = 1,
    UI_CONFIRM_RESET_LATCH = 2,
} UiConfirmAction;

// ── P0.3 + P1.7: FullUiSnapshot — everything Core1 needs ──────
// Core0 builds this atomically under render_lock.
// Core1 renders ONLY from this snapshot — zero access to live structs.
typedef struct {
    // ── Battery data (P0.3) ──────────────────────────────────
    BatSnapshot bat;

    // ── Alarm + fault state (P1.7) ───────────────────────────
    uint32_t alarms;
    uint32_t latched_faults;

    // ── Port states (P1.7) ───────────────────────────────────
    bool port_on[PORT_COUNT];

    // ── System state ─────────────────────────────────────────
    bool safe_mode;
    bool startup_ok;
    bool    charger_present;
    uint8_t meas_valid;       // FMEA-02: sensor validity bitmask

    // ── Stats summary for history screen (P1.7) ──────────────
    float    efc_total;
    float    energy_in_wh;
    float    energy_out_wh;
    float    soh_last;
    float    temp_min_c;
    float    temp_max_c;
    float    temp_avg_c;
    float    peak_current_a;
    float    peak_power_w;
    float    session_h;
    float    session_peak_a;
    float    session_peak_w;
    uint32_t boot_count;
    uint32_t total_alarm_events;
    uint32_t total_ocp_events;
    uint32_t total_temp_events;

    // ── Log cache (P1.7) — last 9 events, pre-fetched by Core0 ──
    uint32_t log_total;
    struct {
        uint32_t timestamp_s;
        uint8_t  type;
        uint8_t  soc_pct;
        uint8_t  temp_bat;
        float    voltage;
        uint32_t alarm_flags;
    } log_cache[9];
    uint8_t  log_cache_n;   // valid entries (0..9)

    // ── I2C scan results ─────────────────────────────────────
    uint8_t  scan_found[8][16];
    uint8_t  scan_counts[8];
    bool     scan_valid;
    bool     confirm_active;
    uint8_t  confirm_kind;
    int8_t   confirm_arg;

    // ── UI state ─────────────────────────────────────────────
    UiState  state;
    int8_t   cur[S_COUNT];
    int8_t   ev_scroll;
    int8_t   hist_page;
    float    soc_anim;
    bool     blink;
    bool     screensaver_active;
    uint16_t anim_phase;
    uint32_t idle_ms;
    char     toast_msg[48];
    uint32_t toast_end_ms;
    bool     poweroff_pending;
    uint32_t poweroff_remaining_ms;
    uint8_t  brightness;
    bool     buzzer_en;
} FullUiSnapshot;

typedef struct {
    Display      *disp;
    PowerControl *pwr;
    PowerSeq     *pseq;    // for user-initiated power-off
    Buzzer       *buz;
    BatSnapshot  *snap;     // pointer to g_bat_snapshot (Core0 writes only)
    Protection   *prot;
    BmsStats     *stats;
    BmsLogger    *logger;   // P1.7: logger reference for log cache building

    UiState  state;
    bool     dirty;

    volatile int8_t nav_delta;
    bool     ok_btn_held;
    uint32_t ok_btn_press_ms;
    uint32_t btn_last_ms[3];
    bool     btn_prev[3];

    int8_t   cur[S_COUNT];
    int8_t   ev_scroll;
    int8_t   hist_page;

    char     toast_msg[48];
    uint32_t toast_end_ms;

    uint8_t  scan_found[8][16];
    uint8_t  scan_counts[8];
    bool     scan_valid;
    bool     confirm_active;
    uint8_t  confirm_kind;
    int8_t   confirm_arg;

    float    soc_anim;
    bool     blink;
    uint32_t blink_ms;
    uint32_t anim_phase_ms;
    uint16_t anim_phase;
    uint32_t last_activity_ms;
    bool     screensaver_active;

    uint8_t  brightness;
    bool     buzzer_en;

    bool     startup_ok;

    // Power-off long-hold tracking
    bool     poweroff_armed;
    uint32_t poweroff_arm_ms;

    // P0.3 + P1.7: render snapshot + its lock
    FullUiSnapshot rs;
    spin_lock_t   *render_lock;
    uint           render_lock_num;
} UI;

void ui_init(UI *ui, Display *d, PowerControl *pwr, PowerSeq *pseq,
             Buzzer *buz, BatSnapshot *snap, Protection *prot,
             BmsStats *stats, BmsLogger *logger);

void ui_poll  (UI *ui);     // Core0
void ui_render(UI *ui);     // Core1

void ui_toast(UI *ui, const char *msg);
void ui_set_startup_ok(UI *ui, bool ok);
void ui_set_state(UI *ui, UiState s);
void ui_refresh(UI *ui);
