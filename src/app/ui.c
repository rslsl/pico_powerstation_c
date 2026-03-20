// ============================================================
// app/ui.c — UI State Machine
//
// v4.0 fixes:
//   P0.3  — BatSnapshot embedded in FullUiSnapshot.
//            _rs_publish() (Core0) copies bat data into rs.bat.
//            Core1 render functions NEVER access ui->snap or
//            g_bat_snapshot — only rs.bat.
//
//   P1.7  — All live data structures (prot, pwr, stats, logger)
//            read by Core0 in _rs_publish() and stored in rs.
//            Core1 render path reads ONLY from FullUiSnapshot.
//
//   P3.12 — Splash text: "Reboot after fixing fault" (was wrong)
//
//   P2.8  — Safe mode badge on all screens.
// ============================================================
#include "ui.h"
#include "config.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "../drivers/tca9548a.h"
#include "../bms/bms_logger.h"
#include "ui_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ── Parent state table ────────────────────────────────────────
static const UiState _PARENT[S_COUNT] = {
    S_MAIN, S_MAIN, S_MAIN, S_ADVANCED, S_MAIN,
    S_PORTS, S_UI_CFG, S_ADVANCED, S_ADVANCED, S_ADVANCED,
};
static const bool _IS_INFO[S_COUNT] = {1,1,1,0,0,0,0,0,0,0};
static const bool _IS_MENU[S_COUNT] = {0,0,0,0,1,1,1,0,0,0};
static const PortId _PORT_MENU_IDS[] = {
    PORT_DC_OUT, PORT_USB_PD, PORT_FAN
};
#define PORT_MENU_COUNT ((int)(sizeof(_PORT_MENU_IDS) / sizeof(_PORT_MENU_IDS[0])))

static void _rs_publish(UI *ui);

static bool _can_hold_poweroff(UiState s) {
    return s == S_MAIN || s == S_STATS || s == S_BATTERY || s == S_DIAGNOSTICS;
}

static const char *_port_menu_label(int idx) {
    switch (idx) {
        case 0: return "DC OUT";
        case 1: return "USB PD";
        case 2: return "FAN";
        default: return "OUTPUT";
    }
}

static void _confirm_open(UI *ui, UiConfirmAction kind, int arg) {
    ui->confirm_active = true;
    ui->confirm_kind = (uint8_t)kind;
    ui->confirm_arg = (int8_t)arg;
    _rs_publish(ui);
}

static void _confirm_close(UI *ui) {
    ui->confirm_active = false;
    ui->confirm_kind = UI_CONFIRM_NONE;
    ui->confirm_arg = -1;
    _rs_publish(ui);
}

static void _confirm_apply(UI *ui) {
    if (!ui->confirm_active) return;

    if ((UiConfirmAction)ui->confirm_kind == UI_CONFIRM_TOGGLE_PORT &&
        ui->confirm_arg >= 0 && ui->confirm_arg < PORT_MENU_COUNT) {
        pwr_toggle(ui->pwr, _PORT_MENU_IDS[ui->confirm_arg]);
        ui_toast(ui, _port_menu_label(ui->confirm_arg));
    } else if ((UiConfirmAction)ui->confirm_kind == UI_CONFIRM_RESET_LATCH) {
        uint32_t resettable = ui->prot ? prot_resettable_latch_bits(ui->prot, ui->prot->latched) : 0;
        if (resettable) {
            prot_reset_latch(ui->prot, resettable);
            ui_toast(ui, "Latched faults reset");
        } else {
            ui_toast(ui, "Fault still active");
        }
    }

    ui->confirm_active = false;
    ui->confirm_kind = UI_CONFIRM_NONE;
    ui->confirm_arg = -1;
    _rs_publish(ui);
}

// ── Buttons (3-button UI) ────────────────────────────────────
// ── P0.3 + P1.7: Atomically publish FullUiSnapshot ───────────
// Called by Core0. Reads ALL data needed by Core1 and copies it
// into rs under render_lock. Core1 then copies rs atomically.
static void _rs_publish(UI *ui) {
    uint32_t save = spin_lock_blocking(ui->render_lock);
    FullUiSnapshot *rs = &ui->rs;

    // ── Battery data (P0.3) ──────────────────────────────────
    if (ui->snap) rs->bat = *ui->snap;

    // ── Alarm + fault state (P1.7) ───────────────────────────
    rs->alarms         = ui->prot ? ui->prot->alarms       : 0;
    rs->latched_faults = ui->prot ? ui->prot->latched      : 0;

    // ── Port states (P1.7) ───────────────────────────────────
    for (int i = 0; i < PORT_COUNT; i++)
        rs->port_on[i] = ui->pwr ? pwr_is_on(ui->pwr, (PortId)i) : false;

    // ── System state ─────────────────────────────────────────
    rs->safe_mode  = ui->pwr ? pwr_is_safe_mode(ui->pwr) : false;
    rs->startup_ok = ui->startup_ok;
    rs->charger_present = ui->pwr ? pwr_is_charger_present(ui->pwr) : false;
    rs->meas_valid = ui->snap ? ui->snap->meas_valid : 0;

    // ── Stats summary (P1.7) ─────────────────────────────────
    if (ui->stats && ui->stats->initialized) {
        const StatsFlash *f = &ui->stats->flash;
        rs->efc_total          = f->efc_total;
        rs->energy_in_wh       = f->energy_in_wh;
        rs->energy_out_wh      = f->energy_out_wh;
        rs->soh_last           = f->soh_last;
        rs->temp_min_c         = f->temp_min_c;
        rs->temp_max_c         = f->temp_max_c;
        rs->temp_avg_c         = stats_avg_temp_c(ui->stats);
        rs->peak_current_a     = f->peak_current_a;
        rs->peak_power_w       = f->peak_power_w;
        rs->boot_count         = f->boot_count;
        rs->total_alarm_events = f->total_alarm_events;
        rs->total_ocp_events   = f->total_ocp_events;
        rs->total_temp_events  = f->total_temp_events;
        rs->session_h          = stats_session_hours();
        rs->session_peak_a     = ui->stats->session_peak_a;
        rs->session_peak_w     = ui->stats->session_peak_w;
    }

    // ── Log cache (P1.7) ─────────────────────────────────────
    // Pre-fetch last 9 log entries so Core1 never touches g_logger.
    if (ui->logger) {
        uint32_t total = log_count(ui->logger);
        rs->log_total    = total;
        rs->log_cache_n  = 0;
        int scroll = ui->ev_scroll;
        for (int i = 0; i < 9; i++) {
            int idx = (int)total - 1 - scroll - i;
            if (idx < 0) break;
            LogEvent ev;
            if (!log_read(ui->logger, (uint32_t)idx, &ev)) break;
            rs->log_cache[i].timestamp_s = ev.timestamp_s;
            rs->log_cache[i].type        = ev.type;
            rs->log_cache[i].soc_pct     = ev.soc_pct;
            rs->log_cache[i].temp_bat    = ev.temp_bat;
            rs->log_cache[i].voltage     = ev.voltage;
            rs->log_cache[i].alarm_flags = ev.alarm_flags;
            rs->log_cache_n++;
        }
    }

    // ── UI state ─────────────────────────────────────────────
    rs->state       = ui->state;
    rs->ev_scroll   = ui->ev_scroll;
    rs->hist_page   = ui->hist_page;
    rs->soc_anim    = ui->soc_anim;
    rs->blink       = ui->blink;
    rs->screensaver_active = ui->screensaver_active;
    rs->anim_phase  = ui->anim_phase;
    rs->idle_ms     = to_ms_since_boot(get_absolute_time()) - ui->last_activity_ms;
    rs->brightness  = ui->brightness;
    rs->buzzer_en   = ui->buzzer_en;
    rs->toast_end_ms= ui->toast_end_ms;
    rs->poweroff_pending = false;
    rs->poweroff_remaining_ms = 0;
    rs->scan_valid  = ui->scan_valid;
    rs->confirm_active = ui->confirm_active;
    rs->confirm_kind = ui->confirm_kind;
    rs->confirm_arg = ui->confirm_arg;
    if (ui->ok_btn_held && _can_hold_poweroff(ui->state)) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        uint32_t held = now - ui->ok_btn_press_ms;
        if (held >= BTN_LONG_MS && held < BTN_POWEROFF_MS) {
            rs->poweroff_pending = true;
            rs->poweroff_remaining_ms = BTN_POWEROFF_MS - held;
        }
    }
    memcpy(rs->cur,        ui->cur,        sizeof(ui->cur));
    memcpy(rs->toast_msg,  ui->toast_msg,  sizeof(ui->toast_msg));
    memcpy(rs->scan_found, ui->scan_found, sizeof(ui->scan_found));
    memcpy(rs->scan_counts,ui->scan_counts,sizeof(ui->scan_counts));

    ui->dirty = true;
    spin_unlock(ui->render_lock, save);
}

// ── Init ─────────────────────────────────────────────────────
void ui_init(UI *ui, Display *d, PowerControl *pwr, PowerSeq *pseq,
             Buzzer *buz, BatSnapshot *snap, Protection *prot,
             BmsStats *stats, BmsLogger *logger) {
    memset(ui, 0, sizeof(*ui));
    ui->disp       = d;
    ui->pwr        = pwr;
    ui->pseq       = pseq;
    ui->buz        = buz;
    ui->snap       = snap;
    ui->prot       = prot;
    ui->stats      = stats;
    ui->logger     = logger;
    ui->state      = S_MAIN;
    ui->dirty      = true;
    ui->brightness = UI_BL_ACTIVE_PCT;
    ui->buzzer_en  = true;
    ui->soc_anim   = snap ? snap->soc : 50.0f;
    ui->blink      = true;
    ui->last_activity_ms = to_ms_since_boot(get_absolute_time());
    ui->blink_ms   = ui->last_activity_ms;
    ui->anim_phase_ms = ui->last_activity_ms;
    ui->screensaver_active = false;
    ui->anim_phase = 0;

    gpio_init(BTN_UP_PIN);   gpio_set_dir(BTN_UP_PIN,  GPIO_IN); gpio_pull_up(BTN_UP_PIN);
    gpio_init(BTN_OK_PIN);   gpio_set_dir(BTN_OK_PIN,  GPIO_IN); gpio_pull_up(BTN_OK_PIN);
    gpio_init(BTN_DOWN_PIN); gpio_set_dir(BTN_DOWN_PIN, GPIO_IN); gpio_pull_up(BTN_DOWN_PIN);

    ui->render_lock_num = spin_lock_claim_unused(true);
    ui->render_lock     = spin_lock_instance(ui->render_lock_num);

    _rs_publish(ui);
}

void ui_set_startup_ok(UI *ui, bool ok) {
    ui->startup_ok = ok;
    _rs_publish(ui);
}

void ui_set_state(UI *ui, UiState s) {
    ui->state = s;
    _rs_publish(ui);
}

void ui_refresh(UI *ui) {
    if (ui->snap) ui->soc_anim = ui->snap->soc;
    _rs_publish(ui);
}

void ui_toast(UI *ui, const char *msg) {
    strncpy(ui->toast_msg, msg, sizeof(ui->toast_msg)-1);
    ui->toast_msg[sizeof(ui->toast_msg)-1] = '\0';
    ui->toast_end_ms = to_ms_since_boot(get_absolute_time()) + 1800;
    _rs_publish(ui);
}

// ── Navigation ───────────────────────────────────────────────
static void _go(UI *ui, UiState s) {
    ui->state = s;
    if (ui->buz) buz_play(ui->buz, BUZ_KEY_CLICK);
    _rs_publish(ui);
}

static void _mark_activity(UI *ui, uint32_t now) {
    ui->last_activity_ms = now;
    if (ui->screensaver_active) {
        ui->screensaver_active = false;
        ui->brightness = UI_BL_ACTIVE_PCT;
        st7789_set_brightness(ui->disp, ui->brightness);
        ui->dirty = true;
    }
}

static void _anim_tick(UI *ui, uint32_t now) {
    bool charging = ui->pwr && pwr_is_charger_present(ui->pwr) && ui->snap &&
                    ui->snap->is_charging && ui->snap->i_net < -0.15f;
    bool idle = (now - ui->last_activity_ms) >= UI_IDLE_SCREENSAVER_MS;
    bool want_saver = charging && idle;
    if (want_saver != ui->screensaver_active) {
        ui->screensaver_active = want_saver;
        ui->brightness = want_saver ? UI_BL_DIM_PCT : UI_BL_ACTIVE_PCT;
        st7789_set_brightness(ui->disp, ui->brightness);
        ui->dirty = true;
    }

    if ((now - ui->blink_ms) >= UI_BLINK_STEP_MS) {
        ui->blink = !ui->blink;
        ui->blink_ms = now;
        ui->dirty = true;
    }

    if ((now - ui->anim_phase_ms) >= UI_ANIM_STEP_MS) {
        ui->anim_phase = (uint16_t)((ui->anim_phase + 10u) % 360u);
        ui->anim_phase_ms = now;
        ui->dirty = true;
    }
}

// FIX #2: extern TCA9548A g_tca (object, not pointer)
static void _start_scan(UI *ui) {
    extern TCA9548A g_tca;
    memset(ui->scan_found,  0, sizeof(ui->scan_found));
    memset(ui->scan_counts, 0, sizeof(ui->scan_counts));
    tca_scan(&g_tca, ui->scan_found, ui->scan_counts);
    ui->scan_valid = true;
    _go(ui, S_I2C_SCAN);
}

static void _click(UI *ui) {
    if (ui->buz) buz_play(ui->buz, BUZ_KEY_CLICK);
    if (ui->confirm_active) {
        _confirm_apply(ui);
        return;
    }
    switch (ui->state) {
        case S_MAIN: case S_STATS: case S_BATTERY: case S_DIAGNOSTICS:
            _go(ui, S_PORTS); break;
        case S_PORTS: {
            int c = ui->cur[S_PORTS];
            if (c < PORT_MENU_COUNT) { _confirm_open(ui, UI_CONFIRM_TOGGLE_PORT, c); }
            else _go(ui, S_UI_CFG);
            break;
        }
        case S_UI_CFG: {
            int c = ui->cur[S_UI_CFG];
            if (c == 0) {
                ui->brightness += 25; if (ui->brightness > 100) ui->brightness = 20;
                if (!ui->screensaver_active) st7789_set_brightness(ui->disp, ui->brightness);
            } else if (c == 1) { ui->buzzer_en = !ui->buzzer_en; buz_set_enabled(ui->buz, ui->buzzer_en); }
            else { _go(ui, S_ADVANCED); return; }
            _rs_publish(ui); break;
        }
        case S_ADVANCED: {
            int c = ui->cur[S_ADVANCED];
            if      (c == 0) _go(ui, S_DIAGNOSTICS);
            else if (c == 1) _go(ui, S_EVENTS);
            else if (c == 2) _go(ui, S_HISTORY);
            else if (c == 3) _start_scan(ui);
            else _rs_publish(ui);
            break;
        }
        case S_HISTORY:
            ui->hist_page = (ui->hist_page + 1) % 3;
            _rs_publish(ui); break;
        default: break;
    }
}

static void _long_press(UI *ui) {
    if (ui->confirm_active) {
        _confirm_close(ui);
        return;
    }
    if (ui->buz) buz_play(ui->buz, BUZ_KEY_LONG);
    _go(ui, _PARENT[ui->state]);
}

// ── Poll (Core0) ─────────────────────────────────────────────
void ui_poll(UI *ui) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    _anim_tick(ui, now);

    const uint8_t pins[3] = { BTN_UP_PIN, BTN_OK_PIN, BTN_DOWN_PIN };
    int nav_step = 0;
    for (int i = 0; i < 3; i++) {
        bool pressed = (gpio_get(pins[i]) == 0);
        if (pressed != ui->btn_prev[i] && (now - ui->btn_last_ms[i]) >= BTN_DEBOUNCE_MS) {
            ui->btn_last_ms[i] = now;
            ui->btn_prev[i] = pressed;
            _mark_activity(ui, now);

            if (i == 1) {
                if (pressed && !ui->ok_btn_held) {
                    ui->ok_btn_held = true;
                    ui->ok_btn_press_ms = now;
                } else if (!pressed && ui->ok_btn_held) {
                    ui->ok_btn_held = false;
                    uint32_t dur = now - ui->ok_btn_press_ms;
                    if (_can_hold_poweroff(ui->state) && dur >= BTN_LONG_MS) {
                        if (dur < BTN_POWEROFF_MS) ui_toast(ui, "Power off canceled");
                    } else if (dur >= BTN_LONG_MS) {
                        _long_press(ui);
                    } else {
                        _click(ui);
                    }
                    ui->poweroff_armed = false;
                }
            } else if (pressed) {
                nav_step += (i == 0) ? -1 : +1;
            }
        }
    }

    if (nav_step != 0) {
        _mark_activity(ui, now);
        if (ui->confirm_active) {
            _confirm_close(ui);
            nav_step = 0;
        }
        if (nav_step == 0) {
            // Navigation input cancels confirmation without moving focus.
        } else
        if (_IS_INFO[ui->state]) {
            int s = (int)ui->state + (nav_step > 0 ? 1 : -1);
            if (s < S_MAIN)        s = S_BATTERY;
            if (s > S_BATTERY)     s = S_MAIN;
            _go(ui, (UiState)s);
        } else if (_IS_MENU[ui->state]) {
            int mx = (ui->state == S_PORTS)    ? PORT_MENU_COUNT + 1 :
                     (ui->state == S_UI_CFG)   ? 3 :
                     (ui->state == S_ADVANCED) ? 4 : 1;
            int c = (int)ui->cur[ui->state] + (nav_step > 0 ? 1 : -1);
            if (c < 0)  c = mx - 1;
            if (c >= mx) c = 0;
            ui->cur[ui->state] = (int8_t)c;
            _rs_publish(ui);
        } else if (ui->state == S_EVENTS) {
            int total = ui->logger ? (int)log_count(ui->logger) : 0;
            int max_scroll = (total > 8) ? (total - 8) : 0;
            ui->ev_scroll += (nav_step > 0 ? 1 : -1);
            if (ui->ev_scroll < 0) ui->ev_scroll = 0;
            if (ui->ev_scroll > max_scroll) ui->ev_scroll = (int8_t)max_scroll;
            _rs_publish(ui);
        } else if (ui->state == S_HISTORY) {
            ui->hist_page += (nav_step > 0 ? 1 : -1);
            if (ui->hist_page < 0) ui->hist_page = 2;
            if (ui->hist_page > 2) ui->hist_page = 0;
            _rs_publish(ui);
        }
    }

    if (ui->ok_btn_held) {
        uint32_t held = now - ui->ok_btn_press_ms;
        if (!ui->confirm_active && ui->state == S_DIAGNOSTICS &&
            ui->prot && ui->prot->latched &&
            held >= (uint32_t)BTN_LONG_MS) {
            _confirm_open(ui, UI_CONFIRM_RESET_LATCH, 0);
            ui->ok_btn_held = false;
            return;
        }
        if (ui->confirm_active) {
            ui->dirty = true;
            return;
        }
        if (_can_hold_poweroff(ui->state) && held >= (uint32_t)BTN_LONG_MS) {
            ui->dirty = true;
        }
        if (_can_hold_poweroff(ui->state) && held >= (uint32_t)BTN_POWEROFF_MS) {
            if (!ui->poweroff_armed) {
                ui->poweroff_armed = true;
                ui->poweroff_arm_ms = now;
                if (ui->buz) buz_play(ui->buz, BUZ_ALARM_WARN);
            }
            if (ui->pseq) {
                printf("[UI] user power-off\n");
                pseq_user_poweroff(ui->pseq, ui->buz);
            }
        }
    }

    if (ui->dirty) _rs_publish(ui);
}

// ── Screen renderers — ALL use rs snapshot, NO live struct access ──

// Safe mode badge (top-right corner)
static void _badge_safe(Display *d, const FullUiSnapshot *rs) {
    if (!rs->safe_mode) return;
    if (rs->blink) disp_fill_rect(d, 142, 1, 86, 12, D_ORANGE);
    disp_text_right_safe(d, 2, rs->startup_ok ? "SAFE MODE" : "INIT FAIL", D_BG);
}

// FMEA-02: stale sensor badge
static void _badge_stale(Display *d, const FullUiSnapshot *rs) {
    // If any critical group (CELLS or TBAT) is invalid → warn
    bool cells_stale = !(rs->meas_valid & MEAS_VALID_CELLS);
    bool tbat_stale  = !(rs->meas_valid & MEAS_VALID_TBAT);
    if (!cells_stale && !tbat_stale) return;
    if (rs->blink) {
        disp_fill_rect(d, 0, 264, ST7789_W, 14, D_RED);
        disp_text_center_safe(d, 265,
                         cells_stale ? "! CELL DATA STALE !" : "! TEMP STALE !",
                         D_WHITE);
    }
}

// Latched fault badge
static void _badge_latched(Display *d, const FullUiSnapshot *rs) {
    if (!rs->latched_faults || rs->safe_mode) return;
    if (rs->blink) disp_text_right_safe(d, 2, "LATCHED", D_RED);
}

static void _fill_circle(Display *d, int cx, int cy, int r, uint16_t col) {
    for (int yy = -r; yy <= r; yy++)
        for (int xx = -r; xx <= r; xx++)
            if (xx*xx + yy*yy <= r*r) disp_pixel(d, cx + xx, cy + yy, col);
}

static void _draw_icon(Display *d, int x, int y, const UiIcon *icon, uint16_t col, int scale) {
    disp_bitmap_1bit(d, x, y, icon->w, icon->h, icon->bits, col, scale);
}

static void _draw_output_tile(Display *d, int x, int y, int w, int h,
                              const UiIcon *icon, const char *label,
                              bool on, uint16_t accent) {
    uint16_t frame = on ? accent : 0x39C7u;
    uint16_t fill = on ? 0x0140u : 0x2124u;
    uint16_t text = on ? D_WHITE : D_SUBTEXT;
    disp_fill_rect(d, x, y, w, h, fill);
    disp_rect(d, x, y, w, h, frame);
    _draw_icon(d, x + 4, y + 5, icon, on ? D_WHITE : D_GRAY, 1);
    disp_text(d, x + 20, y + 7, label, text);
}


static void __attribute__((unused)) _draw_charge_flow(Display *d, int x, int y, int w, int h, uint16_t col, uint16_t bg, uint16_t phase) {
    disp_fill_rect(d, x, y, w, h, bg);
    disp_rect(d, x, y, w, h, D_SUBTEXT);
    int seg = 12;
    int offs = phase % seg;
    for (int i = -seg; i < w; i += seg) {
        int sx = x + i + offs;
        for (int dx = 0; dx < seg/2; dx++) {
            int px = sx + dx;
            if (px >= x + 1 && px < x + w - 1) {
                int top = y + 2 + dx / 2;
                int bot = y + h - 3 - dx / 2;
                if (top <= bot) disp_vline(d, px, top, bot - top + 1, col);
            }
        }
    }
}

static const char *_main_status_text(const BatSnapshot *b) {
    if (b->is_charging) return "Charging";
    if (b->i_dis > 0.3f) return "Powering devices";
    return "Ready";
}

static const char *_main_state_short(const BatSnapshot *b) {
    if (b->is_charging) return "CHARGE";
    if (b->i_dis > 0.3f) return "OUTPUT";
    return "READY";
}

static uint16_t _main_status_color(const BatSnapshot *b) {
    if (b->is_charging) return D_GREEN;
    if (b->i_dis > 0.3f) return D_ORANGE;
    return D_TEXT;
}

static const UiIcon *_main_status_icon(const BatSnapshot *b) {
    if (b->is_charging) return &UI_ICON_CHARGING;
    if (b->i_dis > 0.3f) return &UI_ICON_DISCHARGING;
    return &UI_ICON_STANDBY;
}

static const char *_balance_text(float delta_mv) {
    if (delta_mv > DELTA_CUT_MV) return "Cell balance needs attention";
    if (delta_mv > DELTA_WARN_MV) return "Cell balance slightly uneven";
    return "Cells balanced";
}

static uint16_t _balance_color(float delta_mv) {
    if (delta_mv > DELTA_CUT_MV) return D_RED;
    if (delta_mv > DELTA_WARN_MV) return D_ORANGE;
    return D_GREEN;
}

static const char *_balance_state_short(float delta_mv) {
    if (delta_mv > DELTA_CUT_MV) return "SERVICE";
    if (delta_mv > DELTA_WARN_MV) return "WATCH";
    return "BALANCED";
}

static uint16_t _soh_color(float soh) {
    if (soh >= 85.0f) return D_GREEN;
    if (soh >= 75.0f) return D_ORANGE;
    return D_RED;
}

static const char *_soh_grade(float soh) {
    if (soh >= 95.0f) return "EXCELLENT";
    if (soh >= 85.0f) return "GOOD";
    if (soh >= 75.0f) return "FAIR";
    return "SERVICE";
}

static uint16_t _resistance_color(float r0_mohm) {
    if (r0_mohm <= 30.0f) return D_GREEN;
    if (r0_mohm <= 45.0f) return D_ORANGE;
    return D_RED;
}

static const char *_resistance_state(float r0_mohm) {
    if (r0_mohm <= 18.0f) return "LOW";
    if (r0_mohm <= 30.0f) return "NOMINAL";
    if (r0_mohm <= 45.0f) return "ELEVATED";
    return "HIGH";
}

static const uint16_t UI_BG_DARK   = 0x0004u;
static const uint16_t UI_BG_PANEL  = 0x08A6u;
static const uint16_t UI_BG_PANEL2 = 0x10E8u;
static const uint16_t UI_GRID_DIM  = 0x0127u;
static const uint16_t UI_BLUE_DIM  = 0x11D8u;
static const uint16_t UI_NEON_BLUE = 0x2DFFu;
static const uint16_t UI_NEON_GRN  = 0x4FEAu;
static const uint16_t UI_NEON_AMB  = 0xFD40u;
static const uint16_t UI_GLASS     = 0x214Du;
static const uint16_t UI_SILVER    = 0xCE79u;

static float _clampf_ui(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int _text_w(const char *s, int scale) {
    return (int)strlen(s) * 8 * scale;
}

static void _text_center_box(Display *d, int x, int w, int y,
                             const char *s, uint16_t col, int scale) {
    int tx = x + (w - _text_w(s, scale)) / 2;
    if (scale == 2) disp_text2x(d, tx, y, s, col);
    else disp_text(d, tx, y, s, col);
}

static void _fill_ellipse(Display *d, int cx, int cy, int rx, int ry, uint16_t col) {
    if (rx <= 0 || ry <= 0) return;
    for (int yy = -ry; yy <= ry; yy++) {
        float t = 1.0f - ((float)(yy * yy) / (float)(ry * ry));
        if (t < 0.0f) continue;
        int span = (int)(sqrtf(t) * (float)rx + 0.5f);
        disp_hline(d, cx - span, cy + yy, span * 2 + 1, col);
    }
}

static void _draw_ellipse(Display *d, int cx, int cy, int rx, int ry, uint16_t col) {
    if (rx <= 0 || ry <= 0) return;
    const float two_pi = 6.2831853f;
    const float step = 1.0f / (float)((rx > ry) ? rx : ry);
    for (float a = 0.0f; a < two_pi; a += step) {
        int px = cx + (int)(cosf(a) * (float)rx + 0.5f);
        int py = cy + (int)(sinf(a) * (float)ry + 0.5f);
        disp_pixel(d, px, py, col);
    }
}

static void _draw_line(Display *d, int x0, int y0, int x1, int y1, uint16_t col) {
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        disp_pixel(d, x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void _draw_grid_background(Display *d) {
    disp_fill(d, UI_BG_DARK);
    disp_fill_rect(d, 8, 8, ST7789_W - 16, ST7789_H - 16, D_BG);
    disp_rect(d, 8, 8, ST7789_W - 16, ST7789_H - 16, UI_BLUE_DIM);
    disp_hline(d, 16, 30, ST7789_W - 32, UI_GRID_DIM);
}

static void _draw_panel_box(Display *d, int x, int y, int w, int h,
                            uint16_t edge, uint16_t accent, uint16_t fill,
                            bool hot) {
    if (w <= 4 || h <= 4) return;
    disp_fill_rect(d, x, y, w, h, fill);
    disp_rect(d, x, y, w, h, hot ? accent : edge);
    if (hot) disp_hline(d, x + 1, y + 1, w - 2, accent);
}

static void __attribute__((unused)) _draw_section_block(Display *d, int x, int y, int w, int h,
                                uint16_t fill, uint16_t accent) {
    if (w <= 4 || h <= 4) return;
    disp_fill_rect(d, x, y, w, h, fill);
    disp_hline(d, x + 4, y + 2, w - 8, accent);
    disp_hline(d, x + 4, y + h - 3, w - 8, UI_GRID_DIM);
}

static void __attribute__((unused)) _draw_header_strip(Display *d, const char *title,
                               const char *v1, const char *v2, const char *v3) {
    _draw_panel_box(d, 10, 12, 220, 28, UI_BLUE_DIM, UI_NEON_BLUE, UI_BG_PANEL, true);
    if (v1 || v2 || v3) {
        disp_text(d, 16, 22, title, D_WHITE);
        disp_vline(d, 70, 18, 16, UI_BLUE_DIM);
        disp_vline(d, 124, 18, 16, UI_BLUE_DIM);
        disp_vline(d, 178, 18, 16, UI_BLUE_DIM);
        if (v1) _text_center_box(d, 72, 50, 22, v1, D_WHITE, 1);
        if (v2) _text_center_box(d, 126, 50, 22, v2, D_WHITE, 1);
        if (v3) _text_center_box(d, 180, 44, 22, v3, D_WHITE, 1);
    } else {
        _text_center_box(d, 10, 220, 22, title, D_WHITE, 1);
    }
}

static uint16_t _accent_fill(uint16_t accent) {
    if (accent == UI_NEON_GRN || accent == D_GREEN) return 0x03E7u;
    if (accent == UI_NEON_AMB || accent == D_ORANGE || accent == D_YELLOW) return 0x9BA0u;
    if (accent == D_RED) return 0x9808u;
    return 0x0358u;
}

static void _draw_symbol_detail(Display *d, int x, int y, int w, int h,
                                const UiIcon *icon, uint16_t primary,
                                uint16_t secondary, uint16_t bg) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    int r = (w < h ? w : h) / 2 - 2;

    if (icon == &UI_ICON_DC) {
        _draw_ellipse(d, cx, cy, r, r, primary);
        disp_fill_rect(d, cx - 2, y + 1, 4, h / 3, bg);
        disp_vline(d, cx, y + 1, h / 3 + 2, primary);
        _draw_line(d, cx, cy + r - 2, cx + r / 2, cy + r / 3, secondary);
        return;
    }
    if (icon == &UI_ICON_PD) {
        int rx = x + 3, ry = cy - 4, rw = w - 6, rh = 8;
        disp_rect(d, rx, ry, rw, rh, primary);
        _fill_ellipse(d, rx + 4, cy, 3, 3, bg);
        _fill_ellipse(d, rx + rw - 5, cy, 3, 3, bg);
        disp_hline(d, rx + 5, cy, rw - 10, secondary);
        disp_vline(d, cx, ry + 2, rh - 4, secondary);
        return;
    }
    if (icon == &UI_ICON_FAN) {
        _fill_circle(d, cx, cy, 2, secondary);
        _fill_ellipse(d, cx - 5, cy - 4, 6, 3, primary);
        _fill_ellipse(d, cx + 5, cy - 4, 6, 3, primary);
        _fill_ellipse(d, cx - 5, cy + 4, 6, 3, primary);
        _fill_ellipse(d, cx + 5, cy + 4, 6, 3, primary);
        _draw_line(d, cx - 1, cy - 1, x + 3, y + 3, secondary);
        _draw_line(d, cx + 1, cy - 1, x + w - 4, y + 3, secondary);
        _draw_line(d, cx - 1, cy + 1, x + 3, y + h - 4, secondary);
        _draw_line(d, cx + 1, cy + 1, x + w - 4, y + h - 4, secondary);
        return;
    }
    if (icon == &UI_ICON_CHARGE) {
        disp_rect(d, cx - 5, y + 4, 10, 9, primary);
        disp_vline(d, cx - 2, y + 1, 4, primary);
        disp_vline(d, cx + 2, y + 1, 4, primary);
        disp_vline(d, cx, y + 13, 5, secondary);
        _draw_line(d, cx, y + 18, cx - 3, y + h - 2, secondary);
        return;
    }
    if (icon == &UI_ICON_BATTERY) {
        disp_rect(d, x + 2, y + 4, w - 6, h - 8, primary);
        disp_fill_rect(d, x + w - 4, cy - 3, 2, 6, primary);
        for (int i = 0; i < 4; i++) {
            disp_fill_rect(d, x + 5 + i * 4, y + 7, 2, h - 14, (i < 3) ? primary : secondary);
        }
        return;
    }
    if (icon == &UI_ICON_CELLS) {
        for (int i = 0; i < 3; i++) {
            int bx = x + 1 + i * (w / 3);
            disp_rect(d, bx, y + 4, 6, h - 8, primary);
            disp_fill_rect(d, bx + 2, y + 2, 2, 2, secondary);
            disp_fill_rect(d, bx + 1, y + h / 2, 4, h / 2 - 5, (i == 1) ? secondary : primary);
        }
        return;
    }
    if (icon == &UI_ICON_THERMO) {
        disp_vline(d, cx, y + 2, h - 8, primary);
        disp_vline(d, cx - 1, y + 4, h - 10, primary);
        _fill_circle(d, cx, y + h - 4, 4, primary);
        _fill_circle(d, cx, y + h - 4, 2, secondary);
        disp_hline(d, cx + 4, y + 5, 3, secondary);
        disp_hline(d, cx + 4, y + 9, 3, secondary);
        return;
    }
    if (icon == &UI_ICON_SHIELD) {
        _draw_line(d, cx, y + 1, x + w - 3, y + 5, primary);
        _draw_line(d, x + w - 3, y + 5, x + w - 5, y + h - 7, primary);
        _draw_line(d, x + w - 5, y + h - 7, cx, y + h - 2, primary);
        _draw_line(d, cx, y + h - 2, x + 4, y + h - 7, primary);
        _draw_line(d, x + 4, y + h - 7, x + 2, y + 5, primary);
        _draw_line(d, x + 2, y + 5, cx, y + 1, primary);
        _draw_line(d, x + 5, cy, cx - 1, y + h - 5, secondary);
        _draw_line(d, cx - 1, y + h - 5, x + w - 5, y + 6, secondary);
        return;
    }
    if (icon == &UI_ICON_CLOCK) {
        _draw_ellipse(d, cx, cy, r, r, primary);
        _draw_line(d, cx, cy, cx, y + 4, secondary);
        _draw_line(d, cx, cy, x + w - 5, cy + 2, secondary);
        _fill_circle(d, cx, cy, 1, secondary);
        return;
    }
    if (icon == &UI_ICON_SETTINGS) {
        _draw_ellipse(d, cx, cy, r - 1, r - 1, primary);
        _draw_ellipse(d, cx, cy, r / 2, r / 2, secondary);
        disp_fill_rect(d, cx - 2, y, 4, 3, primary);
        disp_fill_rect(d, cx - 2, y + h - 3, 4, 3, primary);
        disp_fill_rect(d, x, cy - 2, 3, 4, primary);
        disp_fill_rect(d, x + w - 3, cy - 2, 3, 4, primary);
        return;
    }
    if (icon == &UI_ICON_BELL) {
        _draw_ellipse(d, cx, cy - 2, r - 2, r - 3, primary);
        disp_vline(d, x + 4, cy - 2, 6, primary);
        disp_vline(d, x + w - 5, cy - 2, 6, primary);
        disp_hline(d, x + 4, cy + 4, w - 8, primary);
        _fill_circle(d, cx, cy + 7, 2, secondary);
        return;
    }
    if (icon == &UI_ICON_LOGS) {
        disp_rect(d, x + 2, y + 2, w - 4, h - 4, primary);
        _draw_line(d, x + 4, y + h - 6, x + 8, y + h - 10, secondary);
        _draw_line(d, x + 8, y + h - 10, x + 12, y + h - 8, secondary);
        _draw_line(d, x + 12, y + h - 8, x + w - 5, y + 5, secondary);
        return;
    }
    if (icon == &UI_ICON_INFO) {
        _draw_ellipse(d, cx, cy, r, r, primary);
        disp_vline(d, cx, cy - 3, 7, secondary);
        _fill_circle(d, cx, y + 4, 1, secondary);
        return;
    }
    if (icon == &UI_ICON_POWER) {
        _draw_ellipse(d, cx, cy, r, r, primary);
        _draw_line(d, cx - 2, y + 4, cx + 1, cy - 1, secondary);
        _draw_line(d, cx + 1, cy - 1, cx - 1, cy - 1, secondary);
        _draw_line(d, cx - 1, cy - 1, cx + 3, cy + 5, secondary);
        _draw_line(d, cx + 3, cy + 5, cx, cy + 5, secondary);
        _draw_line(d, cx, cy + 5, cx + 2, y + h - 5, secondary);
        return;
    }
    if (icon == &UI_ICON_CHARGING) {
        _draw_symbol_detail(d, x, y, w, h, &UI_ICON_BATTERY, primary, secondary, bg);
        _draw_line(d, cx - 1, y + 5, cx + 2, cy, secondary);
        _draw_line(d, cx + 2, cy, cx, cy, secondary);
        _draw_line(d, cx, cy, cx + 1, y + h - 5, secondary);
        return;
    }
    if (icon == &UI_ICON_DISCHARGING) {
        _draw_symbol_detail(d, x, y, w, h, &UI_ICON_BATTERY, primary, secondary, bg);
        _draw_line(d, x + 5, cy, x + w - 7, cy, secondary);
        _draw_line(d, x + w - 7, cy, x + w - 10, cy - 3, secondary);
        _draw_line(d, x + w - 7, cy, x + w - 10, cy + 3, secondary);
        return;
    }
    if (icon == &UI_ICON_STANDBY) {
        _draw_symbol_detail(d, x, y, w, h, &UI_ICON_BATTERY, primary, secondary, bg);
        _fill_circle(d, x + w - 7, y + 6, 2, secondary);
        return;
    }

    _draw_icon(d, x, y, icon, primary, 2);
}

static void _draw_status_pill(Display *d, int x, int y, int w, int h,
                              bool on, uint16_t accent) {
    uint16_t fill = on ? 0x02A4u : 0x18A6u;
    uint16_t edge = on ? accent : UI_BLUE_DIM;
    disp_fill_rect(d, x, y, w, h, fill);
    disp_rect(d, x, y, w, h, edge);
    _text_center_box(d, x, w, y + 4, on ? "ON" : "OFF", on ? D_GREEN : D_TEXT, 2);
}

static void __attribute__((unused)) _draw_metric_card(Display *d, int x, int y, int w, int h,
                              const UiIcon *icon, uint16_t accent,
                              const char *label, const char *value,
                              bool selected) {
    uint16_t fill = selected ? _accent_fill(accent) : UI_BG_PANEL;
    uint16_t icon_col = selected ? D_WHITE : accent;
    uint16_t detail_col = selected ? accent : D_WHITE;
    uint16_t label_col = selected ? D_WHITE : D_TEXT;
    uint16_t value_col = selected ? D_WHITE : accent;
    int icon_box = (h >= 36) ? 20 : 16;
    int text_x = x + icon_box + 14;
    int scale = 1;
    int value_x = text_x;

    _draw_panel_box(d, x, y, w, h, selected ? accent : UI_BLUE_DIM, accent, fill, selected);
    if (icon) _draw_symbol_detail(d, x + 8, y + (h - icon_box) / 2 - 1, icon_box, icon_box, icon, icon_col, detail_col, fill);

    if (h >= 34 && _text_w(value, 2) <= (w - 12)) {
        scale = 2;
        value_x = x + w - _text_w(value, 2) - 8;
        if (value_x < text_x - 4) value_x = text_x - 4;
    }
    disp_text(d, text_x, y + 6, label, label_col);
    if (scale == 2) disp_text2x(d, value_x, y + h - 20, value, value_col);
    else disp_text(d, value_x, y + h - 10, value, value_col);
}

static void __attribute__((unused)) _draw_output_module(Display *d, int x, int y, int w, int h,
                                const char *label, bool on, uint16_t accent) {
    _draw_panel_box(d, x, y, w, h, UI_BLUE_DIM, accent, UI_BG_PANEL, on);
    disp_text(d, x + 12, y + 12, label, D_WHITE);
    _draw_status_pill(d, x + 12, y + h - 28, w - 24, 20, on, accent);
}

static void __attribute__((unused)) _draw_tube_battery(Display *d, int x, int y, int w, int h,
                               float frac, uint16_t liquid, const char *value) {
    int cx = x + w / 2;
    int cap_y = y + 6;
    int body_x = x + 28;
    int body_y = y + 20;
    int body_w = w - 56;
    int body_h = h - 34;
    int value_y;

    disp_fill_rect(d, cx - 18, cap_y, 36, 8, UI_SILVER);
    disp_rect(d, cx - 18, cap_y, 36, 8, D_WHITE);
    disp_fill_rect(d, cx - 8, cap_y - 5, 16, 5, UI_SILVER);
    disp_rect(d, cx - 8, cap_y - 5, 16, 5, D_WHITE);

    disp_fill_rect(d, body_x, body_y, body_w, body_h, UI_GLASS);
    disp_rect(d, body_x - 1, body_y - 1, body_w + 2, body_h + 2, UI_NEON_BLUE);
    disp_rect(d, body_x + 2, body_y + 2, body_w - 4, body_h - 4, UI_BLUE_DIM);
    disp_vline(d, body_x + 4, body_y + 4, body_h - 8, D_WHITE);
    disp_vline(d, body_x + body_w - 5, body_y + 4, body_h - 8, UI_BLUE_DIM);
    disp_hline(d, body_x + 6, body_y + body_h + 4, body_w - 12, UI_BLUE_DIM);

    frac = _clampf_ui(frac, 0.0f, 1.0f);
    int fill_h = (int)((float)(body_h - 10) * frac);
    if (fill_h > 0) {
        int fill_y = body_y + body_h - 5 - fill_h;
        disp_fill_rect(d, body_x + 5, fill_y, body_w - 10, fill_h, liquid);
        disp_hline(d, body_x + 5, fill_y, body_w - 10, D_WHITE);
    }

    value_y = body_y + body_h / 2 - 10;
    if (value_y < body_y + 10) value_y = body_y + 10;
    _text_center_box(d, x, w, value_y, value, D_WHITE, 2);
}

static void __attribute__((unused)) _draw_cell_tube(Display *d, int x, int y, int w, int h,
                            float voltage, const char *label) {
    char value[12];
    int cx = x + w / 2;
    int top_y = y + 10;
    int body_x = x + 6;
    int body_y = y + 18;
    int body_w = w - 12;
    int body_h = h - 34;
    float frac = _clampf_ui((voltage - 3.0f) / 1.2f, 0.0f, 1.0f);
    uint16_t liquid = voltage < CELL_CUT_V ? D_RED :
                      voltage < CELL_WARN_V ? UI_NEON_AMB : UI_NEON_GRN;

    _fill_ellipse(d, cx, top_y, (w / 2) - 5, 5, UI_SILVER);
    disp_fill_rect(d, cx - 6, y + 1, 12, 9, UI_SILVER);
    _draw_ellipse(d, cx, top_y, (w / 2) - 5, 5, UI_NEON_BLUE);
    disp_fill_rect(d, body_x, body_y, body_w, body_h, UI_GLASS);
    disp_rect(d, body_x, body_y, body_w, body_h, UI_NEON_BLUE);
    disp_vline(d, body_x + 2, body_y + 3, body_h - 6, D_WHITE);

    int fill_h = (int)((float)(body_h - 8) * frac);
    if (fill_h > 0) {
        int fill_y = body_y + body_h - 4 - fill_h;
        disp_fill_rect(d, body_x + 3, fill_y, body_w - 6, fill_h, liquid);
        _fill_ellipse(d, cx, fill_y, (body_w / 2) - 4, 4, liquid);
    }

    _text_center_box(d, x - 2, w + 4, y + h - 11, label, D_WHITE, 1);
    snprintf(value, sizeof(value), "%.2fV", voltage);
    _text_center_box(d, x - 6, w + 12, y + h + 3, value, liquid, 1);
}

static void __attribute__((unused)) _draw_diag_chip(Display *d, int x, int y, int w, int h,
                            const char *label, const char *state, uint16_t col,
                            bool selected) {
    uint16_t fill = selected ? _accent_fill(col) : UI_BG_PANEL;
    _draw_panel_box(d, x, y, w, h, selected ? col : UI_BLUE_DIM, col, fill, selected);
    disp_text(d, x + 8, y + 8, label, selected ? D_WHITE : D_TEXT);
    disp_text(d, x + 8, y + 20, state, selected ? D_WHITE : col);
}

static void __attribute__((unused)) _draw_icon_tile(Display *d, int x, int y, int w, int h,
                            const UiIcon *icon, const char *label,
                            const char *state, uint16_t accent, bool selected) {
    uint16_t fill = selected ? _accent_fill(accent) : UI_BG_PANEL;
    uint16_t icon_col = selected ? D_WHITE : accent;
    uint16_t detail_col = selected ? accent : D_WHITE;
    uint16_t label_col = selected ? D_WHITE : D_WHITE;
    uint16_t state_col = selected ? D_WHITE : accent;
    _draw_panel_box(d, x, y, w, h, selected ? accent : UI_BLUE_DIM, accent, fill, selected);
    _draw_symbol_detail(d, x + (w - 28) / 2, y + 12, 28, 28, icon, icon_col, detail_col, fill);
    _text_center_box(d, x, w, y + h - 28, label, label_col, 1);
    if (state && state[0]) {
        disp_fill_rect(d, x + 12, y + h - 16, w - 24, 10, selected ? 0x0006u : UI_BG_PANEL2);
        _text_center_box(d, x, w, y + h - 15, state, state_col, 1);
    }
}

static float _signed_current_a(const BatSnapshot *b) {
    if (b->is_charging) return b->i_chg;
    if (b->i_net < -0.05f && b->i_chg > 0.05f) return b->i_chg;
    if (b->i_dis > 0.05f) return -b->i_dis;
    if (b->i_chg > 0.05f) return b->i_chg;
    return 0.0f;
}

static bool _discharge_display_active(const BatSnapshot *b) {
    return (b->i_net > 0.18f) && (b->i_dis > 0.18f);
}

static bool _input_only_display_active(const FullUiSnapshot *rs, const BatSnapshot *b) {
    return rs->charger_present && b->i_chg > 0.05f &&
           !b->is_charging && !_discharge_display_active(b);
}

static float _display_power_w(const FullUiSnapshot *rs, const BatSnapshot *b) {
    if (b->is_charging || _input_only_display_active(rs, b)) {
        return b->voltage * b->i_chg;
    }
    return b->power_w;
}

static bool _has_runtime_estimate(const BatSnapshot *b) {
    if (b->time_min <= 0 || b->time_min >= 9999) return false;
    if (b->is_charging) return true;
    return b->pred_confidence >= 0.12f;
}

static uint16_t _runtime_color(const BatSnapshot *b) {
    if (b->is_charging) return D_WHITE;
    if (!_has_runtime_estimate(b)) return D_SUBTEXT;
    if (b->pred_confidence >= 0.60f) return D_GREEN;
    return D_WHITE;
}

static void _format_runtime_short(const BatSnapshot *b, char *buf, size_t n) {
    if (_has_runtime_estimate(b)) {
        int minutes = b->time_min;
        if (minutes >= 120) minutes = ((minutes + 2) / 5) * 5;
        else if (minutes >= 30) minutes = ((minutes + 1) / 2) * 2;
        snprintf(buf, n, "%s%dh%02dm",
                 (!b->is_charging && b->pred_confidence < 0.30f) ? "~" : "",
                 minutes / 60, minutes % 60);
    } else {
        snprintf(buf, n, "--:--");
    }
}

static uint16_t _soc_color(float soc) {
    if (soc <= SOC_CUTOFF_PCT) return D_RED;
    if (soc <= SOC_WARN_PCT) return D_ORANGE;
    return D_GREEN;
}

static void _draw_screen_title(Display *d, const char *title, const char *right) {
    disp_text(d, 16, 16, title, D_WHITE);
    if (right && right[0]) disp_text_right_safe(d, 16, right, D_SUBTEXT);
    disp_hline(d, 16, 30, ST7789_W - 32, UI_BLUE_DIM);
}

static void _draw_footer_hint(Display *d, const char *left, const char *right) {
    disp_hline(d, 16, 258, ST7789_W - 32, UI_BLUE_DIM);
    if (left && left[0]) disp_text(d, 16, 264, left, D_TEXT);
    if (right && right[0]) disp_text_right_safe(d, 264, right, D_SUBTEXT);
}

static void _draw_confirm_overlay(Display *d, const FullUiSnapshot *rs) {
    const char *lines[3];
    char prompt[32];

    if (!rs->confirm_active) return;

    if ((UiConfirmAction)rs->confirm_kind == UI_CONFIRM_TOGGLE_PORT) {
        snprintf(prompt, sizeof(prompt), "TOGGLE %s?", _port_menu_label(rs->confirm_arg));
        lines[0] = prompt;
    } else if ((UiConfirmAction)rs->confirm_kind == UI_CONFIRM_RESET_LATCH) {
        lines[0] = "RESET LATCHED FAULTS?";
    } else {
        lines[0] = "CONFIRM ACTION?";
    }
    lines[1] = "OK = CONFIRM";
    lines[2] = "UP/DOWN = CANCEL";
    disp_dialog(d, lines, 3);
}

static void _draw_scrollbar(Display *d, int x, int y, int h, int total, int visible, int scroll) {
    if (total <= visible || h <= 0) return;
    int max_scroll = total - visible;
    int thumb_h = (h * visible) / total;
    if (thumb_h < 16) thumb_h = 16;
    if (thumb_h > h) thumb_h = h;
    int track_h = h - thumb_h;
    int thumb_y = y;
    if (max_scroll > 0 && track_h > 0) {
        thumb_y += (track_h * scroll) / max_scroll;
    }
    disp_fill_rect(d, x, y, 4, h, 0x18C3u);
    disp_fill_rect(d, x, thumb_y, 4, thumb_h, D_ACCENT);
}

static void _format_log_age(uint32_t sec, char *buf, size_t n) {
    if (sec < 60u) snprintf(buf, n, "%lus", (unsigned long)sec);
    else if (sec < 3600u) snprintf(buf, n, "%lum", (unsigned long)(sec / 60u));
    else if (sec < 86400u) snprintf(buf, n, "%luh", (unsigned long)(sec / 3600u));
    else snprintf(buf, n, "%lud", (unsigned long)(sec / 86400u));
}

static const char *_log_type_name(uint8_t type) {
    switch (type) {
        case LOG_BOOT: return "BOOT";
        case LOG_CHARGE_START: return "CHG+";
        case LOG_CHARGE_END: return "CHG-";
        case LOG_DISCHARGE_END: return "DIS-";
        case LOG_ALARM: return "ALARM";
        case LOG_SOC_WARN: return "SOC";
        case LOG_TEMP_WARN: return "TEMP";
        case LOG_OCP: return "OCP";
        case LOG_SAVE: return "SAVE";
        default: return "EVENT";
    }
}

static uint16_t _log_type_color(uint8_t type) {
    switch (type) {
        case LOG_BOOT: return D_ACCENT;
        case LOG_CHARGE_START: return D_GREEN;
        case LOG_CHARGE_END: return D_ORANGE;
        case LOG_DISCHARGE_END: return D_TEXT;
        case LOG_ALARM: return D_RED;
        case LOG_SOC_WARN: return D_ORANGE;
        case LOG_TEMP_WARN: return UI_NEON_AMB;
        case LOG_OCP: return D_RED;
        case LOG_SAVE: return UI_NEON_BLUE;
        default: return D_TEXT;
    }
}

static uint16_t _meter_color(float pos, bool good_when_high) {
    pos = _clampf_ui(pos, 0.0f, 1.0f);
    if (good_when_high) {
        if (pos < 0.25f) return D_RED;
        if (pos < 0.50f) return D_ORANGE;
        if (pos < 0.75f) return D_YELLOW;
        return D_GREEN;
    }
    if (pos < 0.25f) return D_GREEN;
    if (pos < 0.50f) return D_YELLOW;
    if (pos < 0.75f) return D_ORANGE;
    return D_RED;
}

static void _draw_progress_bar(Display *d, int x, int y, int w, int h,
                               float frac, uint16_t col);

static void _draw_vertical_meter(Display *d, int x, int y, int w, int h,
                                 float frac, bool good_when_high) {
    const int segments = 13;
    const int gap = 2;
    int inner_x = x + 3;
    int inner_w = w - 6;
    int seg_h;
    int lit;

    if (w < 8 || h < 32) return;
    frac = _clampf_ui(frac, 0.0f, 1.0f);
    seg_h = (h - 6 - (segments - 1) * gap) / segments;
    if (seg_h < 4) seg_h = 4;
    lit = (int)(frac * (float)segments + 0.5f);
    if (lit < 0) lit = 0;
    if (lit > segments) lit = segments;

    _draw_panel_box(d, x, y, w, h, UI_BLUE_DIM, UI_NEON_BLUE, UI_BG_PANEL, false);
    for (int seg = 0; seg < segments; seg++) {
        int seg_y = y + h - 3 - seg_h - seg * (seg_h + gap);
        float pos = (segments <= 1) ? 0.0f : ((float)seg / (float)(segments - 1));
        uint16_t col = _meter_color(pos, good_when_high);
        bool active = seg < lit;
        disp_fill_rect(d, inner_x, seg_y, inner_w, seg_h, active ? col : UI_BG_PANEL2);
        disp_rect(d, inner_x, seg_y, inner_w, seg_h, active ? col : UI_BLUE_DIM);
    }
}

static void _draw_compact_center_card(Display *d, int x, int y, int w, int h,
                                      const char *label, const char *value,
                                      uint16_t accent, bool large) {
    _draw_panel_box(d, x, y, w, h, UI_BLUE_DIM, accent, UI_BG_PANEL, false);
    disp_text(d, x + 8, y + 6, label, D_SUBTEXT);
    if (large && _text_w(value, 2) <= (w - 10)) {
        _text_center_box(d, x, w, y + h - 21, value, accent, 2);
    } else {
        _text_center_box(d, x, w, y + h - 12, value, accent, 1);
    }
}

static void _draw_cell_overview_card(Display *d, int x, int y, int w, int h,
                                     const char *label, float voltage) {
    float frac = _clampf_ui((voltage - 3.0f) / 1.2f, 0.0f, 1.0f);
    uint16_t col = voltage < CELL_CUT_V ? D_RED :
                   voltage < CELL_WARN_V ? D_ORANGE : D_GREEN;

    _draw_panel_box(d, x, y, w, h, UI_BLUE_DIM, col, UI_BG_PANEL, false);
    _text_center_box(d, x, w, y + 14, label, D_WHITE, 1);
    _draw_progress_bar(d, x + 8, y + h - 16, w - 16, 8, frac, col);
}

static void _draw_value_card(Display *d, int x, int y, int w, int h,
                             const char *label, const char *value,
                             uint16_t accent, bool selected, bool large) {
    uint16_t fill = selected ? _accent_fill(accent) : UI_BG_PANEL;
    uint16_t label_col = selected ? D_WHITE : D_SUBTEXT;
    uint16_t value_col = selected ? D_WHITE : accent;
    _draw_panel_box(d, x, y, w, h, selected ? accent : UI_BLUE_DIM, accent, fill, selected);
    disp_text(d, x + 8, y + 7, label, label_col);
    if (large && _text_w(value, 2) <= (w - 16)) {
        _text_center_box(d, x, w, y + h - 22, value, value_col, 2);
    } else {
        disp_text(d, x + 8, y + h - 12, value, value_col);
    }
}

static void _draw_list_card(Display *d, int x, int y, int w, int h,
                            const char *label, const char *value,
                            uint16_t accent, bool selected) {
    uint16_t fill = selected ? _accent_fill(accent) : UI_BG_PANEL;
    uint16_t label_col = selected ? D_WHITE : D_TEXT;
    uint16_t value_col = selected ? D_WHITE : accent;
    _draw_panel_box(d, x, y, w, h, selected ? accent : UI_BLUE_DIM, accent, fill, selected);
    disp_text(d, x + 10, y + 9, label, label_col);
    disp_text_right_safe(d, y + 9, value, value_col);
}

static void _draw_progress_bar(Display *d, int x, int y, int w, int h,
                               float frac, uint16_t col) {
    int fill_w;
    if (w <= 2 || h <= 2) return;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    disp_fill_rect(d, x, y, w, h, UI_BG_PANEL);
    disp_rect(d, x, y, w, h, UI_BLUE_DIM);
    fill_w = (int)((float)(w - 2) * frac);
    if (fill_w > 0) disp_fill_rect(d, x + 1, y + 1, fill_w, h - 2, col);
}

static void __attribute__((unused)) _draw_cell_card(Display *d, int x, int y, int w, int h,
                            const char *label, float voltage) {
    char buf[12];
    uint16_t col = voltage < CELL_CUT_V ? D_RED :
                   voltage < CELL_WARN_V ? D_ORANGE : D_GREEN;
    float frac = (voltage - 3.0f) / 1.2f;
    _draw_panel_box(d, x, y, w, h, UI_BLUE_DIM, col, UI_BG_PANEL, false);
    disp_text(d, x + 8, y + 7, label, D_SUBTEXT);
    snprintf(buf, sizeof(buf), "%.2fV", voltage);
    disp_text(d, x + 8, y + 22, buf, col);
    _draw_progress_bar(d, x + 8, y + h - 14, w - 16, 8, frac, col);
}

static void _render_charging_saver(Display *d, const FullUiSnapshot *rs) {
    char soc_buf[16];
    char cur_buf[16];
    char volt_buf[16];
    char power_buf[16];
    char eta_buf[32];
    char info_line[56];
    const BatSnapshot *b = &rs->bat;
    uint16_t soc_col = _soc_color(b->soc);
    float charge_power_w = _display_power_w(rs, b);

    _draw_grid_background(d);
    _draw_screen_title(d, "CHARGING", rs->charger_present ? "INPUT DETECTED" : "");
    snprintf(soc_buf, sizeof(soc_buf), "%d%%", (int)b->soc);
    snprintf(cur_buf, sizeof(cur_buf), "+%.1fA", b->i_chg);
    snprintf(volt_buf, sizeof(volt_buf), "%.1fV", b->voltage);
    snprintf(power_buf, sizeof(power_buf), "+%.0fW", charge_power_w);
    snprintf(info_line, sizeof(info_line), "%s   %s   %s", soc_buf, volt_buf, cur_buf);

    _draw_panel_box(d, 12, 42, 216, 74, UI_BLUE_DIM, soc_col, UI_BG_PANEL2, true);
    disp_text(d, 20, 50, "CHARGE STATUS", D_SUBTEXT);
    _draw_progress_bar(d, 20, 72, 188, 12, b->soc / 100.0f, soc_col);
    disp_text_center_safe(d, 92, info_line, D_WHITE);

    if (b->time_min > 0 && b->time_min < 9999) {
        snprintf(eta_buf, sizeof(eta_buf), "TO FULL %dh%02dm", b->time_min / 60, b->time_min % 60);
    } else {
        snprintf(eta_buf, sizeof(eta_buf), "ESTIMATING TIME");
    }

    _draw_value_card(d, 12, 130, 104, 42, "POWER", power_buf, UI_NEON_GRN, false, true);
    _draw_value_card(d, 124, 130, 104, 42, "PACK", volt_buf, D_ACCENT, false, true);
    _draw_list_card(d, 12, 184, 216, 30, "STATUS", "CHARGE ACTIVE", D_GREEN, false);
    _draw_list_card(d, 12, 222, 216, 30, "ETA", eta_buf, D_TEXT, false);
    disp_text_center_safe(d, 254, "PRESS ANY KEY", D_SUBTEXT);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

static void _render_main(Display *d, const FullUiSnapshot *rs) {
    char buf[48];
    const BatSnapshot *b = &rs->bat;

    disp_header(d, "POWERSTATION", FW_VERSION);

    // ── SOC ring gauge (240° arc, start=150°, fills clockwise) ──
    // Arc center (78,110): sweeps lower-left → top → lower-right
    uint16_t soc_col = (b->soc < SOC_CUTOFF_PCT) ? D_RED :
                       (b->soc < SOC_WARN_PCT)    ? D_ORANGE : D_GREEN;
    disp_ring_arc(d, 78, 110, 55, 10, 150.0f, 240.0f,
                  rs->soc_anim / 100.0f, soc_col, 0x18C3u);

    // Large SOC% centered inside ring
    snprintf(buf, sizeof(buf), "%d%%", (int)b->soc);
    int soc_w = (int)(strlen(buf)) * 16;
    disp_text2x(d, 78 - soc_w / 2, 100, buf, soc_col);

    // Status icon + text below ring center (in the open arc area)
    _draw_icon(d, 60, 128, _main_status_icon(b), _main_status_color(b), 1);
    disp_text(d, 76, 129, _main_status_text(b), _main_status_color(b));

    // ── Right panel: pack voltage (2x), energy, power, temp ────
    const int RX = 150;
    snprintf(buf, sizeof(buf), "%.1fV", b->voltage);
    disp_text2x(d, RX, 26, buf, D_ACCENT);

    snprintf(buf, sizeof(buf), "%.0f Wh", b->remaining_wh);
    _draw_icon(d, RX, 62, &UI_ICON_CHARGE, D_TEXT, 1);
    disp_text_safe(d, RX + 16, 62, buf, D_TEXT);

    if (b->is_charging) {
        snprintf(buf, sizeof(buf), "+%.0fW", b->power_w);
        _draw_icon(d, RX, 78, &UI_ICON_CHARGING, D_GREEN, 1);
        disp_text_safe(d, RX + 16, 78, buf, D_GREEN);
    } else if (b->i_dis > 0.3f) {
        snprintf(buf, sizeof(buf), "%.0fW", b->power_w);
        _draw_icon(d, RX, 78, &UI_ICON_DISCHARGING, D_ORANGE, 1);
        disp_text_safe(d, RX + 16, 78, buf, D_ORANGE);
    }

    snprintf(buf, sizeof(buf), "%.0fC", b->temp_bat);
    _draw_icon(d, RX, 94, &UI_ICON_THERMO, D_SUBTEXT, 1);
    disp_text_safe(d, RX + 16, 94, buf, D_SUBTEXT);

    // ── Middle info strip ────────────────────────────────────
    disp_hline(d, 12, 148, ST7789_W - 24, 0x3186u);

    if (b->time_min > 0 && b->time_min < 9999) {
        _draw_icon(d, D_SAFE_LEFT, 154, &UI_ICON_CLOCK, D_ACCENT, 1);
        snprintf(buf, sizeof(buf), "%dh%02dm remaining", b->time_min / 60, b->time_min % 60);
        disp_text_safe(d, D_SAFE_LEFT + 16, 155, buf, D_ACCENT);
    } else if (b->is_charging) {
        _draw_icon(d, D_SAFE_LEFT, 154, &UI_ICON_CLOCK, D_SUBTEXT, 1);
        disp_text_safe(d, D_SAFE_LEFT + 16, 155, "Estimating charge time", D_SUBTEXT);
    } else {
        _draw_icon(d, D_SAFE_LEFT, 154, &UI_ICON_CLOCK, D_SUBTEXT, 1);
        disp_text_safe(d, D_SAFE_LEFT + 16, 155, "No estimate yet", D_SUBTEXT);
    }

    _draw_icon(d, D_SAFE_LEFT, 170, &UI_ICON_CELLS, _balance_color(b->delta_mv), 1);
    disp_text_safe(d, D_SAFE_LEFT + 16, 171, _balance_text(b->delta_mv), _balance_color(b->delta_mv));

    if (rs->charger_present && !b->is_charging) {
        _draw_icon(d, D_SAFE_LEFT, 186, &UI_ICON_CHARGE, D_GREEN, 1);
        disp_text_safe(d, D_SAFE_LEFT + 16, 187, "Charger connected", D_GREEN);
    } else if (b->i_dis > 0.3f) {
        _draw_icon(d, D_SAFE_LEFT, 186, &UI_ICON_POWER, D_ORANGE, 1);
        snprintf(buf, sizeof(buf), "%.1fA  %.0fW load", b->i_dis, b->power_w);
        disp_text_safe(d, D_SAFE_LEFT + 16, 187, buf, D_ORANGE);
    } else {
        _draw_icon(d, D_SAFE_LEFT, 186, &UI_ICON_INFO, D_SUBTEXT, 1);
        disp_text_safe(d, D_SAFE_LEFT + 16, 187, "No active load", D_SUBTEXT);
    }

    // ── Alarm banner (dedicated row — never overwrites other content) ──
    bool crit = (rs->alarms & ALARM_ANY_CRIT) != 0;
    if (crit && rs->blink) {
        disp_fill_rect(d, 0, 202, ST7789_W, 13, D_RED);
        disp_text_center_safe(d, 203, "! CHECK SYSTEM !", D_WHITE);
    } else if (rs->alarms && rs->blink) {
        disp_text_safe(d, D_SAFE_LEFT, 203, "Warnings active", D_ORANGE);
    }

    // ── Output status tiles ──────────────────────────────────
    static const char *pnames[] = {"DC", "PD", "FAN"};
    static const UiIcon *tile_icons[] = {
        &UI_ICON_DC, &UI_ICON_PD, &UI_ICON_FAN
    };
    static const PortId pids[] = {PORT_DC_OUT, PORT_USB_PD, PORT_FAN};
    for (int i = 0; i < 3; i++) {
        bool on = rs->port_on[pids[i]];
        _draw_output_tile(d, 14 + i * 54, 220, 44, 24, tile_icons[i], pnames[i], on, D_GREEN);
    }

    _badge_safe(d, rs);
    _badge_latched(d, rs);
    _badge_stale(d, rs);
    disp_footer(d, "OK=outputs", "Hold OK=off");
}

static void _render_stats_v2(Display *d, const FullUiSnapshot *rs) {
    char buf[32];
    const BatSnapshot *b = &rs->bat;

    disp_header(d, "POWER NOW", NULL);

    struct { const char *lbl; float val; float mx; uint16_t col; const char *unit; } rows[] = {
        {"Charge",    b->soc,      100.0f,     D_ACCENT, "%"},
        {"Output",    b->i_dis,    IMAX_DIS_A, D_ORANGE, "A"},
        {"Charge in", b->i_chg,    IMAX_CHG_A, D_GREEN,  "A"},
        {"Bat temp",  b->temp_bat, 80.0f,      D_RED,    "C"},
        {"Port temp", b->temp_inv, 90.0f,      D_PURPLE, "C"},
    };
    for (int i = 0; i < 5; i++) {
        int y = 26 + i * 24;
        disp_text_safe(d, 12, y + 4, rows[i].lbl, D_SUBTEXT);
        disp_bar(d, 88, y, 94, 16, rows[i].val, rows[i].mx, rows[i].col);
        snprintf(buf, sizeof(buf), (i == 0) ? "%.0f%s" : "%.1f%s", rows[i].val, rows[i].unit);
        disp_text_right_safe(d, y + 4, buf, D_TEXT);
    }

    disp_fill_rect(d, 12, 150, ST7789_W - 24, 1, 0x3186u);
    if (b->is_charging) {
        snprintf(buf, sizeof(buf), "Charging at %.0fW", b->power_w);
        disp_text_safe(d, 16, 160, buf, D_GREEN);
    } else if (b->i_dis > 0.3f) {
        snprintf(buf, sizeof(buf), "Devices use %.0fW", b->power_w);
        disp_text_safe(d, 16, 160, buf, D_ORANGE);
    } else {
        disp_text_safe(d, 16, 160, "System is in standby", D_SUBTEXT);
    }
    snprintf(buf, sizeof(buf), "Stored energy %.0fWh", b->remaining_wh);
    disp_text_safe(d, 16, 178, buf, D_TEXT);
    disp_text_safe(d, 16, 196, rs->charger_present ? "Charger detected" : "No charger connected",
                   rs->charger_present ? D_GREEN : D_SUBTEXT);
    disp_text_safe(d, 16, 214, _balance_text(b->delta_mv), _balance_color(b->delta_mv));

    _badge_safe(d, rs);
    disp_footer(d, "OK=outputs", "Hold OK=off");
}

static void _render_battery_v2(Display *d, const FullUiSnapshot *rs) {
    char buf[40];
    const BatSnapshot *b = &rs->bat;

    disp_header(d, "BATTERY HEALTH", NULL);

    // ── Pack voltage (2x) + SOH bar ──────────────────────────
    snprintf(buf, sizeof(buf), "%.1fV", b->voltage);
    disp_text2x(d, D_SAFE_LEFT, 22, buf, D_ACCENT);

    float soh = b->soh;
    uint16_t soh_col = (soh > 80.0f) ? D_GREEN : (soh > 60.0f) ? D_ORANGE : D_RED;
    snprintf(buf, sizeof(buf), "SOH:%.0f%%", soh);
    disp_text(d, 120, 30, buf, soh_col);
    disp_bar(d, 120, 42, 100, 10, soh, 100.0f, soh_col);

    // ── Vertical cell bars ────────────────────────────────────
    disp_hline(d, 12, 60, ST7789_W - 24, 0x3186u);
    _draw_icon(d, D_SAFE_LEFT, 64, &UI_ICON_CELLS, D_TEXT, 1);
    disp_text_safe(d, D_SAFE_LEFT + 16, 65, "Cell voltages", D_TEXT);

    const float cell_v[3] = {b->v_b1, b->v_b2, b->v_b3};
    static const char *cell_names[3] = {"C1", "C2", "C3"};
    // BAR_Y=92 — gives 14px gap after "Cell voltages" row (64..76), then
    // cell name labels at 92-13=79, bars start at 92.
    const int BAR_W = 52, BAR_H = 88, BAR_Y = 92;
    const int bar_xs[3] = {14, 80, 146};

    for (int i = 0; i < 3; i++) {
        float frac = (cell_v[i] - 3.0f) / 1.2f;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        uint16_t col = cell_v[i] < CELL_CUT_V  ? D_RED :
                       cell_v[i] < CELL_WARN_V ? D_ORANGE : D_GREEN;
        int bx = bar_xs[i];

        // Cell name above bar (centered, y=79 is 13px above bars at 92)
        int lx = bx + (BAR_W - 2 * 8) / 2;
        disp_text(d, lx, BAR_Y - 13, cell_names[i], col);

        // Bar outline + dark fill
        disp_rect(d, bx, BAR_Y, BAR_W, BAR_H, col);
        disp_fill_rect(d, bx + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, 0x1082u);

        // Fill from bottom proportional to voltage fraction
        int fill_h = (int)((BAR_H - 2) * frac);
        if (fill_h > 0)
            disp_fill_rect(d, bx + 1, BAR_Y + BAR_H - 1 - fill_h, BAR_W - 2, fill_h, col);

        // Voltage text below bar ("X.XX" = 4 chars × 8px = 32px, centered in 52px bar)
        snprintf(buf, sizeof(buf), "%.2f", cell_v[i]);
        disp_text(d, bx + (BAR_W - 4 * 8) / 2, BAR_Y + BAR_H + 4, buf, col);
    }

    // ── Bottom stats ─────────────────────────────────────────
    int sy = BAR_Y + BAR_H + 20;  // = 92 + 88 + 20 = 200
    disp_hline(d, 12, sy - 4, ST7789_W - 24, 0x3186u);

    // Row 1: energy left  |  delta voltage
    snprintf(buf, sizeof(buf), "%.0f Wh", b->remaining_wh);
    disp_text_safe(d, D_SAFE_LEFT, sy, buf, D_TEXT);
    uint16_t dc = b->delta_mv > DELTA_CUT_MV ? D_RED :
                  b->delta_mv > DELTA_WARN_MV ? D_ORANGE : D_GREEN;
    snprintf(buf, sizeof(buf), "dV %.0fmV", b->delta_mv);
    disp_text_right_safe(d, sy, buf, dc);

    // Row 2: EFC  |  R0
    snprintf(buf, sizeof(buf), "Cycle %.1f", b->efc);
    disp_text_safe(d, D_SAFE_LEFT, sy + 16, buf, D_SUBTEXT);
    snprintf(buf, sizeof(buf), "R0 %.0fmO", b->r0_mohm);
    disp_text_right_safe(d, sy + 16, buf, D_SUBTEXT);

    // Row 3: current flow
    if (b->is_charging) {
        snprintf(buf, sizeof(buf), "Charging +%.1fA  %.0fW", b->i_chg, b->power_w);
        disp_text_safe(d, D_SAFE_LEFT, sy + 32, buf, D_GREEN);
    } else if (b->i_dis > 0.1f) {
        snprintf(buf, sizeof(buf), "Output %.1fA  %.0fW", b->i_dis, b->power_w);
        disp_text_safe(d, D_SAFE_LEFT, sy + 32, buf, D_ORANGE);
    } else {
        disp_text_safe(d, D_SAFE_LEFT, sy + 32, "Battery resting", D_SUBTEXT);
    }

    _badge_safe(d, rs);
    disp_footer(d, "UP/DN=screen", "OK=outputs");
}

static void _render_diagnostics(Display *d, const FullUiSnapshot *rs) {
    char buf[40];
    const BatSnapshot *b = &rs->bat;
    const uint8_t valid = rs->meas_valid;
    int y = 22;

    disp_header(d, "SYSTEM CHECK", rs->startup_ok ? "ALL OK" : "STARTUP BLOCKED");

    if (!rs->startup_ok) {
        disp_fill_rect(d, 0, 20, ST7789_W, 14, D_RED);
        disp_text_center(d, 23, "SAFE MODE: outputs locked", D_WHITE);
        y = 40;
    }

    struct {
        const char *label;
        bool ok;
        bool warn_only;
    } checks[] = {
        {"PACK INA226 ", (valid & MEAS_VALID_PACK)  != 0, false},
        {"CHG  INA226 ", (valid & MEAS_VALID_CHG)   != 0, false},
        {"CELL INA3221", (valid & MEAS_VALID_CELLS) != 0, false},
        {"TBAT LM75   ", (valid & MEAS_VALID_TBAT)  != 0, true},
        {"TUSB LM75   ", b->inv_temp_sensor_ok && ((valid & MEAS_VALID_TINV) != 0), true},
    };

    for (int i = 0; i < 5; i++, y += 16) {
        uint16_t col = checks[i].ok ? D_GREEN : (checks[i].warn_only ? D_ORANGE : D_RED);
        disp_text(d, 4, y, checks[i].label, D_TEXT);
        disp_text_right(d, y, checks[i].ok ? "OK" : (checks[i].warn_only ? "WARN" : "FAIL"), col);
    }

    y += 4;
    disp_hline(d, 0, y, ST7789_W, D_SUBTEXT);
    y += 8;

    snprintf(buf, sizeof(buf), "VBAT %.2fV  dV %.0fmV", b->voltage, b->delta_mv);
    disp_text(d, 4, y, buf, D_ACCENT); y += 16;
    snprintf(buf, sizeof(buf), "B1 %.3f  B2 %.3f", b->v_b1, b->v_b2);
    disp_text(d, 4, y, buf, D_TEXT); y += 16;
    snprintf(buf, sizeof(buf), "B3 %.3f  CHG %s", b->v_b3, rs->charger_present ? "YES" : "NO");
    disp_text(d, 4, y, buf, D_TEXT); y += 16;
    snprintf(buf, sizeof(buf), "TBAT %.1fC  TUSB %.1fC", b->temp_bat, b->temp_inv);
    disp_text(d, 4, y, buf, D_TEXT); y += 16;
    snprintf(buf, sizeof(buf), "IDIS %.1fA  ICHG %.1fA", b->i_dis, b->i_chg);
    disp_text(d, 4, y, buf, D_TEXT);

    _badge_safe(d, rs);
    _badge_stale(d, rs);
    disp_footer(d, "UP/DN=screen", "OK=ports");
}

static void _render_ports(Display *d, const FullUiSnapshot *rs) {
    int sel = rs->cur[S_PORTS];
    char buf[32];
    const UiIcon *icons[] = {&UI_ICON_DC, &UI_ICON_PD, &UI_ICON_FAN, &UI_ICON_CHARGE};

    disp_header(d, "OUTPUTS", NULL);

    const char *labels[] = {
        "12V output","USB-C fast","Cooling fan","Charging input","Settings >"
    };
    for (int i = 0; i < PORT_MENU_COUNT + 1; i++) {
        int y = 22 + i * 22;
        bool is_sel = (i == sel);
        if (is_sel) disp_fill_rect(d, 0, y-1, ST7789_W, 20, 0x0C30u);
        bool on = (i < PORT_MENU_COUNT) && rs->port_on[_PORT_MENU_IDS[i]];
        uint16_t col = (i == PORT_MENU_COUNT) ? D_ACCENT : on ? D_GREEN : D_GRAY;
        if (i < PORT_MENU_COUNT) _draw_icon(d, D_SAFE_LEFT, y + 4, icons[i], col, 1);
        else _draw_icon(d, D_SAFE_LEFT, y + 4, &UI_ICON_SETTINGS, col, 1);
        disp_text_safe(d, D_SAFE_LEFT + 18, y+4, labels[i], col);
        if (i < PORT_MENU_COUNT) {
            disp_text_right_safe(d, y+4, on ? "ON " : "OFF", col);
            if (on && rs->blink) _fill_circle(d, 182, y + 8, 2, col);
        }
    }
    if (rs->safe_mode)
        disp_text_safe(d, D_SAFE_LEFT, 200, "Safe mode: outputs locked", D_ORANGE);

    snprintf(buf, sizeof(buf), "%d/%d", sel+1, PORT_MENU_COUNT+1);
    disp_footer(d, buf, "OK=toggle  Hold=back");
}

static void _render_ui_cfg(Display *d, const FullUiSnapshot *rs) {
    int sel = rs->cur[S_UI_CFG];
    char bright_buf[8];
    const UiIcon *icons[] = {&UI_ICON_SUN, &UI_ICON_POWER, &UI_ICON_SETTINGS};
    snprintf(bright_buf, sizeof(bright_buf), "%d%%", rs->brightness);

    disp_header(d, "SETTINGS", NULL);

    struct { const char *lbl; const char *val; } items[3] = {
        {"Brightness", bright_buf},
        {"Buzzer",     rs->buzzer_en ? "ON " : "OFF"},
        {"Advanced >", ""},
    };
    for (int i = 0; i < 3; i++) {
        int y = 28 + i * 30;
        if (i == sel) disp_fill_rect(d, 0, y-1, ST7789_W, 26, 0x0C30u);
        _draw_icon(d, D_SAFE_LEFT, y + 6, icons[i], (i==2) ? D_ACCENT : D_TEXT, 1);
        disp_text_safe(d, D_SAFE_LEFT + 18, y+7, items[i].lbl, (i==2) ? D_ACCENT : D_TEXT);
        disp_text_right_safe(d, y+7, items[i].val, D_GREEN);
    }
    disp_footer(d, "Click=change", "Hold OK=back");
}

static void _render_advanced(Display *d, const FullUiSnapshot *rs) {
    int sel = rs->cur[S_ADVANCED];
    const UiIcon *icons[] = {&UI_ICON_SHIELD, &UI_ICON_LOGS, &UI_ICON_BATTERY, &UI_ICON_INFO};
    disp_header(d, "SERVICE", NULL);
    const char *items[] = {"System check >","Event history >","Lifetime stats >","Sensor scan >"};
    for (int i = 0; i < 4; i++) {
        int y = 28 + i * 32;
        if (i == sel) disp_fill_rect(d, 0, y-1, ST7789_W, 26, 0x0C30u);
        _draw_icon(d, D_SAFE_LEFT, y + 6, icons[i], D_ACCENT, 1);
        disp_text_safe(d, D_SAFE_LEFT + 18, y+7, items[i], D_ACCENT);
    }
    disp_footer(d, "Click=open", "Hold OK=back");
}

#pragma GCC diagnostic pop

// P1.7: reads from rs->log_cache (pre-fetched by Core0) — NOT from g_logger
static void _render_main_ref(Display *d, const FullUiSnapshot *rs) {
    char power_buf[16];
    char volt_buf[16];
    char cur_buf[16];
    char eta_buf[20];
    char tbat_buf[12];
    char tsys_buf[12];
    char state_buf[16];
    char volt_top[8];
    char volt_bottom[8];
    char amp_top[8];
    char amp_bottom[8];
    const BatSnapshot *b = &rs->bat;
    bool load_active = _discharge_display_active(b);
    bool chg_input = _input_only_display_active(rs, b);
    float shown_power_w = _display_power_w(rs, b);
    float signed_current = _signed_current_a(b);
    float volt_frac = _clampf_ui((b->voltage - 9.0f) / (BAT_FULL_V - 9.0f), 0.0f, 1.0f);
    float load_frac = _clampf_ui(b->i_dis / 60.0f, 0.0f, 1.0f);
    uint16_t volt_col = _meter_color(volt_frac, true);
    uint16_t cur_col = _meter_color(load_frac, false);
    uint16_t power_col = (b->is_charging || chg_input) ? D_GREEN : (shown_power_w > 140.0f ? D_RED :
                         shown_power_w > 70.0f ? D_ORANGE : D_YELLOW);
    uint16_t eta_col = _runtime_color(b);
    uint16_t state_col = (rs->alarms & ALARM_ANY_CRIT) ? D_RED :
                         rs->alarms ? D_ORANGE :
                         b->is_charging ? D_GREEN :
                         load_active ? D_ORANGE :
                         chg_input ? D_ACCENT : D_TEXT;

    _draw_grid_background(d);
    _draw_screen_title(d, "SYSTEM", NULL);
    if (b->is_charging || chg_input) snprintf(power_buf, sizeof(power_buf), "+%.0fW", shown_power_w);
    else snprintf(power_buf, sizeof(power_buf), "%.0fW", shown_power_w);
    snprintf(volt_buf, sizeof(volt_buf), "%.1fV", b->voltage);
    snprintf(cur_buf, sizeof(cur_buf), "%+.1fA", signed_current);
    _format_runtime_short(b, eta_buf, sizeof(eta_buf));
    snprintf(tbat_buf, sizeof(tbat_buf), "%.0fC", b->temp_bat);
    snprintf(tsys_buf, sizeof(tsys_buf), "%.0fC", b->temp_inv);
    snprintf(volt_top, sizeof(volt_top), "%.1f", BAT_FULL_V);
    snprintf(volt_bottom, sizeof(volt_bottom), "9.0");
    snprintf(amp_top, sizeof(amp_top), "60A");
    snprintf(amp_bottom, sizeof(amp_bottom), "0A");

    if ((rs->alarms & ALARM_ANY_CRIT) && rs->blink) snprintf(state_buf, sizeof(state_buf), "ALARM");
    else if (rs->alarms) snprintf(state_buf, sizeof(state_buf), "WARN");
    else if (b->is_charging) snprintf(state_buf, sizeof(state_buf), "CHG");
    else if (load_active) snprintf(state_buf, sizeof(state_buf), "LOAD");
    else if (chg_input) snprintf(state_buf, sizeof(state_buf), "INPUT");
    else snprintf(state_buf, sizeof(state_buf), "READY");

    disp_text_safe(d, 8, 42, volt_top, D_SUBTEXT);
    disp_text_safe(d, 8, 214, volt_bottom, D_SUBTEXT);
    disp_text_right_safe(d, 42, amp_top, D_SUBTEXT);
    disp_text_right_safe(d, 214, amp_bottom, D_SUBTEXT);
    _draw_vertical_meter(d, 12, 56, 16, 158, volt_frac, true);
    _draw_vertical_meter(d, 212, 56, 16, 158, load_frac, false);

    _draw_compact_center_card(d, 40, 42, 160, 44, "POWER", power_buf, power_col, true);
    _draw_compact_center_card(d, 40, 92, 160, 38, "VOLTAGE", volt_buf, volt_col, true);
    _draw_compact_center_card(d, 40, 136, 160, 38,
                              b->is_charging ? "CHARGING" : (chg_input ? "CHG IN" : "CURRENT"),
                              cur_buf,
                              b->is_charging ? D_GREEN : (chg_input ? D_ACCENT : cur_col),
                              true);
    _draw_compact_center_card(d, 40, 180, 160, 34, b->is_charging ? "TIME TO FULL" : "TIME LEFT",
                              eta_buf, eta_col, true);

    _draw_compact_center_card(d, 40, 224, 50, 28, "TBAT", tbat_buf,
                              (b->temp_bat >= TEMP_BAT_WARN_C) ? D_ORANGE : D_YELLOW, false);
    _draw_compact_center_card(d, 95, 224, 50, 28, "STATE", state_buf, state_col, false);
    _draw_compact_center_card(d, 150, 224, 50, 28, "TSYS", tsys_buf,
                              (b->temp_inv >= TEMP_INV_WARN_C) ? D_ORANGE : D_YELLOW, false);

    _badge_safe(d, rs);
    _badge_latched(d, rs);
    _badge_stale(d, rs);
}

static void _render_stats_ref(Display *d, const FullUiSnapshot *rs) {
    char c1_buf[12];
    char c2_buf[12];
    char c3_buf[12];
    char volt_buf[16];
    char current_buf[16];
    char power_buf[16];
    char energy_buf[16];
    char delta_buf[16];
    const BatSnapshot *b = &rs->bat;
    bool input_active = b->is_charging || _input_only_display_active(rs, b);
    uint16_t io_col = input_active ? D_GREEN : D_ORANGE;

    _draw_grid_background(d);
    _draw_screen_title(d, "BATTERY INFO", NULL);

    snprintf(c1_buf, sizeof(c1_buf), "%.2fV", b->v_b1);
    snprintf(c2_buf, sizeof(c2_buf), "%.2fV", b->v_b2);
    snprintf(c3_buf, sizeof(c3_buf), "%.2fV", b->v_b3);
    snprintf(volt_buf, sizeof(volt_buf), "%.1fV", b->voltage);
    snprintf(current_buf, sizeof(current_buf), "%+.1fA", _signed_current_a(b));
    snprintf(power_buf, sizeof(power_buf), "%+.0fW",
             (b->is_charging || _input_only_display_active(rs, b)) ? _display_power_w(rs, b) : b->power_w);
    snprintf(energy_buf, sizeof(energy_buf), "%.0fWH", b->remaining_wh);
    snprintf(delta_buf, sizeof(delta_buf), "%.0fMV", b->delta_mv);

    _draw_cell_overview_card(d, 12, 42, 68, 44, "C1", b->v_b1);
    _draw_cell_overview_card(d, 86, 42, 68, 44, "C2", b->v_b2);
    _draw_cell_overview_card(d, 160, 42, 68, 44, "C3", b->v_b3);

    _draw_compact_center_card(d, 12, 92, 68, 28, "CELL 1", c1_buf, _meter_color(_clampf_ui((b->v_b1 - 3.0f) / 1.2f, 0.0f, 1.0f), true), false);
    _draw_compact_center_card(d, 86, 92, 68, 28, "CELL 2", c2_buf, _meter_color(_clampf_ui((b->v_b2 - 3.0f) / 1.2f, 0.0f, 1.0f), true), false);
    _draw_compact_center_card(d, 160, 92, 68, 28, "CELL 3", c3_buf, _meter_color(_clampf_ui((b->v_b3 - 3.0f) / 1.2f, 0.0f, 1.0f), true), false);

    _draw_compact_center_card(d, 12, 132, 216, 40, "PACK VOLTAGE", volt_buf, _soc_color(b->soc), true);
    _draw_compact_center_card(d, 12, 180, 104, 34, "CURRENT", current_buf, io_col, true);
    _draw_compact_center_card(d, 124, 180, 104, 34, "POWER", power_buf, io_col, true);
    _draw_compact_center_card(d, 12, 220, 104, 34, "ENERGY", energy_buf, D_ACCENT, true);
    _draw_compact_center_card(d, 124, 220, 104, 34, "DELTA", delta_buf, _balance_color(b->delta_mv), true);

    _badge_safe(d, rs);
    _badge_stale(d, rs);
}

static void _render_battery_ref(Display *d, const FullUiSnapshot *rs) {
    char soh_buf[16];
    char delta_buf[16];
    char efc_buf[16];
    char r0_buf[16];
    char state_buf[16];
    uint16_t soh_col;
    uint16_t bal_col;
    uint16_t r0_col;
    const BatSnapshot *b = &rs->bat;

    _draw_grid_background(d);
    _draw_screen_title(d, "BATTERY HEALTH", NULL);

    snprintf(soh_buf, sizeof(soh_buf), "%.0f%%", b->soh);
    snprintf(delta_buf, sizeof(delta_buf), "%.0fmV", b->delta_mv);
    snprintf(efc_buf, sizeof(efc_buf), "%.1f", b->efc);
    snprintf(r0_buf, sizeof(r0_buf), "%.0fmO", b->r0_mohm);
    snprintf(state_buf, sizeof(state_buf), "%s", _main_state_short(b));
    soh_col = _soh_color(b->soh);
    bal_col = _balance_color(b->delta_mv);
    r0_col = _resistance_color(b->r0_mohm);

    _draw_panel_box(d, 12, 42, 216, 54, UI_BLUE_DIM, soh_col, UI_BG_PANEL2, true);
    disp_text(d, 20, 50, "SOH", D_SUBTEXT);
    disp_text2x(d, 20, 66, soh_buf, soh_col);
    disp_text(d, 148, 52, "GRADE", D_SUBTEXT);
    disp_text(d, 148, 70, _soh_grade(b->soh), soh_col);
    _draw_progress_bar(d, 20, 84, 188, 8, b->soh / 100.0f, soh_col);

    _draw_value_card(d, 12, 108, 104, 42, "CYCLES", efc_buf, D_ACCENT, false, true);
    _draw_value_card(d, 124, 108, 104, 42, "DELTA", delta_buf, bal_col, false, true);
    _draw_value_card(d, 12, 160, 104, 42, "R0", r0_buf, r0_col, false, true);
    _draw_value_card(d, 124, 160, 104, 42, "STATE", state_buf, _main_status_color(b), false, true);

    _draw_list_card(d, 12, 214, 216, 18, "CELL MATCH", _balance_state_short(b->delta_mv), bal_col, false);
    _draw_list_card(d, 12, 238, 216, 18, "RESISTANCE", _resistance_state(b->r0_mohm), r0_col, false);

    _badge_safe(d, rs);
    _badge_stale(d, rs);
}

static void _render_diagnostics_ref(Display *d, const FullUiSnapshot *rs) {
    char volt_buf[16];
    char delta_buf[16];
    char temp_buf[20];
    char current_buf[20];
    const char *pack_state;
    const char *chg_state;
    const char *cells_state;
    const char *tbat_state;
    const char *tinv_state;
    const BatSnapshot *b = &rs->bat;
    const uint8_t valid = rs->meas_valid;
    int base_y = rs->startup_ok ? 42 : 68;
    const char *title_right = !rs->startup_ok ? "STARTUP BLOCKED" :
                              (rs->latched_faults ? "HOLD OK" : NULL);

    _draw_grid_background(d);
    _draw_screen_title(d, "SYSTEM CHECK", title_right);

    if (!rs->startup_ok) {
        _draw_list_card(d, 12, 42, 216, 22, "MODE", "SAFE MODE", D_RED, false);
    }

    pack_state = (valid & MEAS_VALID_PACK) ? "OK" : "FAIL";
    chg_state = (valid & MEAS_VALID_CHG) ? "OK" : "WARN";
    cells_state = (valid & MEAS_VALID_CELLS) ? "OK" : "FAIL";
    tbat_state = (valid & MEAS_VALID_TBAT) ? "OK" : "WARN";
    tinv_state = (b->inv_temp_sensor_ok && (valid & MEAS_VALID_TINV)) ? "OK" :
                 (b->inv_temp_sensor_ok ? "WARN" : "MISSING");

    _draw_list_card(d, 12, base_y + 0, 216, 20, "PACK INA226", pack_state, (valid & MEAS_VALID_PACK) ? D_GREEN : D_RED, false);
    _draw_list_card(d, 12, base_y + 24, 216, 20, "CHARGE INA226", chg_state, (valid & MEAS_VALID_CHG) ? D_GREEN : D_ORANGE, false);
    _draw_list_card(d, 12, base_y + 48, 216, 20, "CELL MONITOR", cells_state, (valid & MEAS_VALID_CELLS) ? D_GREEN : D_RED, false);
    _draw_list_card(d, 12, base_y + 72, 216, 20, "BAT TEMP", tbat_state, (valid & MEAS_VALID_TBAT) ? D_GREEN : D_ORANGE, false);
    _draw_list_card(d, 12, base_y + 96, 216, 20, "USB TEMP", tinv_state,
                    (b->inv_temp_sensor_ok && (valid & MEAS_VALID_TINV)) ? D_GREEN : D_ORANGE, false);

    snprintf(volt_buf, sizeof(volt_buf), "%.2fV", b->voltage);
    snprintf(delta_buf, sizeof(delta_buf), "%.0fmV", b->delta_mv);
    snprintf(temp_buf, sizeof(temp_buf), "%.1f/%.1fC", b->temp_bat, b->temp_inv);
    snprintf(current_buf, sizeof(current_buf), "%.1f/%.1fA", b->i_dis, b->i_chg);

    _draw_value_card(d, 12, 194, 104, 34, "VBAT", volt_buf, D_ACCENT, false, false);
    _draw_value_card(d, 124, 194, 104, 34, "DELTA", delta_buf, _balance_color(b->delta_mv), false, false);
    _draw_value_card(d, 12, 234, 104, 26, "TEMP", temp_buf, UI_NEON_AMB, false, false);
    _draw_value_card(d, 124, 234, 104, 26, "I DIS/CHG", current_buf, D_ORANGE, false, false);

    _badge_safe(d, rs);
    _badge_stale(d, rs);
}

static void _render_ports_ref(Display *d, const FullUiSnapshot *rs) {
    int sel = rs->cur[S_PORTS];
    const BatSnapshot *b = &rs->bat;
    const char *chg_text = rs->charger_present ? (b->is_charging ? "MEASURING" : "DETECTED") : "NOT DETECTED";
    uint16_t chg_col = rs->charger_present ? (b->is_charging ? D_GREEN : D_ORANGE) : D_SUBTEXT;

    _draw_grid_background(d);
    _draw_screen_title(d, "OUTPUTS", NULL);
    _draw_list_card(d, 12, 44, 216, 34, "DC OUT", rs->port_on[PORT_DC_OUT] ? "ON" : "OFF",
                    rs->port_on[PORT_DC_OUT] ? D_GREEN : D_SUBTEXT, sel == 0);
    _draw_list_card(d, 12, 84, 216, 34, "USB PD", rs->port_on[PORT_USB_PD] ? "ON" : "OFF",
                    rs->port_on[PORT_USB_PD] ? D_GREEN : D_SUBTEXT, sel == 1);
    _draw_list_card(d, 12, 124, 216, 34, "FAN", rs->port_on[PORT_FAN] ? "ON" : "OFF",
                    rs->port_on[PORT_FAN] ? D_GREEN : D_SUBTEXT, sel == 2);
    _draw_list_card(d, 12, 164, 216, 34, "SETTINGS", "OPEN",
                    UI_NEON_BLUE, sel == 3);
    _draw_list_card(d, 12, 214, 216, 24, "CHARGE INPUT", chg_text, chg_col, false);
    _draw_list_card(d, 12, 244, 216, 18, "NOTE", "INPUT IS SENSOR ONLY", D_SUBTEXT, false);
    _badge_safe(d, rs);
}

static void _render_ui_cfg_ref(Display *d, const FullUiSnapshot *rs) {
    int sel = rs->cur[S_UI_CFG];
    char bright_buf[8];

    _draw_grid_background(d);
    _draw_screen_title(d, "SETTINGS", NULL);

    snprintf(bright_buf, sizeof(bright_buf), "%d%%", rs->brightness);
    _draw_list_card(d, 12, 52, 216, 40, "BRIGHTNESS", bright_buf, UI_NEON_AMB, sel == 0);
    _draw_list_card(d, 12, 102, 216, 40, "BUZZER", rs->buzzer_en ? "ON" : "OFF",
                    rs->buzzer_en ? D_GREEN : D_SUBTEXT, sel == 1);
    _draw_list_card(d, 12, 152, 216, 40, "SERVICE MENU", "OPEN", UI_NEON_BLUE, sel == 2);
    _draw_list_card(d, 12, 214, 216, 22, "ACTION", (sel < 2) ? "OK TO CHANGE" : "OK TO OPEN",
                    D_TEXT, false);
}

static void _render_advanced_ref(Display *d, const FullUiSnapshot *rs) {
    int sel = rs->cur[S_ADVANCED];

    _draw_grid_background(d);
    _draw_screen_title(d, "SERVICE", NULL);
    _draw_list_card(d, 12, 44, 216, 34, "SYSTEM CHECK", "OPEN", UI_NEON_GRN, sel == 0);
    _draw_list_card(d, 12, 84, 216, 34, "EVENT HISTORY", "OPEN", UI_NEON_BLUE, sel == 1);
    _draw_list_card(d, 12, 124, 216, 34, "LIFETIME STATS", "OPEN", UI_NEON_AMB, sel == 2);
    _draw_list_card(d, 12, 164, 216, 34, "SENSOR SCAN", "OPEN", UI_NEON_BLUE, sel == 3);
    _draw_list_card(d, 12, 214, 216, 22, "ACTION", "OK TO OPEN", D_TEXT, false);
}

static void _render_events(Display *d, const FullUiSnapshot *rs) {
    char title_right[16];
    char age_buf[8];
    char label_buf[20];
    char value_buf[20];

    _draw_grid_background(d);
    if (rs->log_total > 0) snprintf(title_right, sizeof(title_right), "%lu EV", (unsigned long)rs->log_total);
    else title_right[0] = '\0';
    _draw_screen_title(d, "EVENT LOG", title_right);

    if (rs->log_total == 0 || rs->log_cache_n == 0) {
        _draw_list_card(d, 12, 118, 216, 24, "STATUS", "NO EVENTS", D_SUBTEXT, false);
    } else {
        int shown = rs->log_cache_n < 8 ? rs->log_cache_n : 8;
        for (int row = 0; row < shown; row++) {
            _format_log_age(rs->log_cache[row].timestamp_s, age_buf, sizeof(age_buf));
            snprintf(label_buf, sizeof(label_buf), "%s  %s", age_buf, _log_type_name(rs->log_cache[row].type));
            snprintf(value_buf, sizeof(value_buf), "%d%%  %.2fV",
                     rs->log_cache[row].soc_pct, rs->log_cache[row].voltage);
            _draw_list_card(d, 12, 42 + row * 24, 216, 20, label_buf, value_buf,
                            _log_type_color(rs->log_cache[row].type), false);
        }
        _draw_scrollbar(d, 228, 42, shown * 24 - 4, (int)rs->log_total, shown, rs->ev_scroll);
    }

    _draw_footer_hint(d, "OK=BACK", "ROT=SCROLL");
    _badge_safe(d, rs);
}

// ── History screen — P1.7: reads only from rs stats fields ───

static void _render_history_p0(Display *d, const FullUiSnapshot *rs) {
    char efc_buf[16];
    char soh_buf[16];
    char in_buf[16];
    char out_buf[16];
    char eff_buf[16];
    char boots_buf[16];
    char alarms_buf[16];
    char session_buf[16];
    float eff = (rs->energy_in_wh > 1.0f)
                ? rs->energy_out_wh / rs->energy_in_wh * 100.0f : 0.0f;
    float soh = rs->soh_last;
    uint16_t soh_col = (soh > 80.0f) ? D_GREEN : (soh > 60.0f) ? D_ORANGE : D_RED;
    uint16_t eff_col = (eff > 85.0f) ? D_GREEN : (eff > 75.0f) ? D_ORANGE : D_RED;

    _draw_grid_background(d);
    _draw_screen_title(d, "LIFETIME STATS", "OVERVIEW");

    snprintf(efc_buf, sizeof(efc_buf), "%.1f", rs->efc_total);
    snprintf(soh_buf, sizeof(soh_buf), "%.0f%%", soh);
    snprintf(in_buf, sizeof(in_buf), "%.0fWH", rs->energy_in_wh);
    snprintf(out_buf, sizeof(out_buf), "%.0fWH", rs->energy_out_wh);
    snprintf(eff_buf, sizeof(eff_buf), "%.1f%%", eff);
    snprintf(boots_buf, sizeof(boots_buf), "%lu", (unsigned long)rs->boot_count);
    snprintf(alarms_buf, sizeof(alarms_buf), "%lu", (unsigned long)rs->total_alarm_events);
    snprintf(session_buf, sizeof(session_buf), "%.0fM", rs->session_h * 60.0f);

    _draw_value_card(d, 12, 42, 104, 42, "CYCLES", efc_buf, D_TEXT, false, true);
    _draw_value_card(d, 124, 42, 104, 42, "SOH", soh_buf, soh_col, false, true);
    _draw_progress_bar(d, 20, 94, 188, 10, soh / 100.0f, soh_col);
    _draw_list_card(d, 12, 112, 216, 20, "ENERGY IN", in_buf, D_GREEN, false);
    _draw_list_card(d, 12, 136, 216, 20, "ENERGY OUT", out_buf, D_ORANGE, false);
    _draw_list_card(d, 12, 160, 216, 20, "EFFICIENCY", eff_buf, eff_col, false);
    _draw_list_card(d, 12, 184, 216, 20, "BOOTS", boots_buf, D_TEXT, false);
    _draw_list_card(d, 12, 208, 216, 20, "ALARMS", alarms_buf,
                    rs->total_alarm_events ? D_RED : D_SUBTEXT, false);
    _draw_list_card(d, 12, 232, 216, 20, "THIS SESSION", session_buf, D_TEXT, false);

    _draw_footer_hint(d, "1/3 OVERVIEW", "ROT/CLICK");
}

static void __attribute__((unused)) _render_history_p1_legacy(Display *d, const FullUiSnapshot *rs) {
    char buf[40]; int y = 22;

    disp_text_safe(d, D_SAFE_LEFT, y, "THERMAL & HEALTH", D_ACCENT); y += 14;
    disp_hline(d, 0, y, ST7789_W, D_SUBTEXT); y += 6;

    snprintf(buf, sizeof(buf), "Temp min: %.0f°C", rs->temp_min_c);
    disp_text(d, 4, y, buf, D_ACCENT); y += 13;

    uint16_t ac = (rs->temp_avg_c > 45.0f) ? D_RED :
                  (rs->temp_avg_c > 35.0f) ? D_ORANGE : D_GREEN;
    snprintf(buf, sizeof(buf), "Temp avg: %.0f°C", rs->temp_avg_c);
    disp_text(d, 4, y, buf, ac); y += 13;

    uint16_t mc = (rs->temp_max_c > TEMP_BAT_CUT_C) ? D_RED :
                  (rs->temp_max_c > TEMP_BAT_WARN_C) ? D_ORANGE : D_TEXT;
    snprintf(buf, sizeof(buf), "Temp max: %.0f°C", rs->temp_max_c);
    disp_text(d, 4, y, buf, mc); y += 14;

    disp_hline(d, 0, y, ST7789_W, D_SUBTEXT); y += 5;

    snprintf(buf, sizeof(buf), "Peak I: %.1f A", rs->peak_current_a);
    disp_text(d, 4, y, buf, D_ORANGE); y += 13;
    snprintf(buf, sizeof(buf), "Peak P: %.0f W", rs->peak_power_w);
    disp_text(d, 4, y, buf, D_ORANGE); y += 14;

    disp_hline(d, 0, y, ST7789_W, D_SUBTEXT); y += 5;

    snprintf(buf, sizeof(buf), "Alarms: %lu  OCP: %lu  OTP: %lu",
             (unsigned long)rs->total_alarm_events,
             (unsigned long)rs->total_ocp_events,
             (unsigned long)rs->total_temp_events);
    disp_text(d, 4, y, buf, D_RED);

    disp_footer(d, "2/3 thermal", "Rot/Click=page");
}

static void __attribute__((unused)) _render_history_p2_legacy(Display *d, const FullUiSnapshot *rs) {
    char buf[40];
    char tte_buf[20];
    int y = 22;
    const BatSnapshot *b = &rs->bat;

    disp_text_safe(d, D_SAFE_LEFT, y, "PREDICTIONS", D_ACCENT); y += 14;
    disp_hline(d, 0, y, ST7789_W, D_SUBTEXT); y += 6;

    if (_has_runtime_estimate(b)) {
        _format_runtime_short(b, tte_buf, sizeof(tte_buf));
        snprintf(buf, sizeof(buf), "TTE: %s", tte_buf);
        disp_text(d, 4, y, buf, _runtime_color(b));
    } else {
        disp_text(d, 4, y, "TTE: ---", D_SUBTEXT);
    }
    y += 14;

    float soh_deg = 100.0f - rs->soh_last;
    float cyc_per_pct = (soh_deg > 0.5f && rs->efc_total > 5.0f)
                        ? rs->efc_total / soh_deg : 10.0f;
    float soh_remain  = rs->soh_last - 80.0f;
    int   cyc_remain  = (soh_remain > 0.0f) ? (int)(soh_remain * cyc_per_pct) : 0;
    uint16_t cr = (cyc_remain > 500) ? D_GREEN : (cyc_remain > 200) ? D_ORANGE : D_RED;
    snprintf(buf, sizeof(buf), "Est. remain: ~%d cycles", cyc_remain);
    disp_text(d, 4, y, buf, cr); y += 14;

    if (rs->efc_total > 1.0f && rs->boot_count > 5) {
        float h_per_efc = (rs->session_h > 0.1f)
                          ? rs->session_h / rs->efc_total : 100.0f;
        float eol_h = cyc_remain * h_per_efc;
        if      (eol_h > 8760.0f) snprintf(buf, sizeof(buf), "EOL: ~%.0f yr", eol_h/8760.0f);
        else if (eol_h > 720.0f)  snprintf(buf, sizeof(buf), "EOL: ~%.0f mo", eol_h/720.0f);
        else                       snprintf(buf, sizeof(buf), "EOL: ~%.0f hr", eol_h);
        disp_text(d, 4, y, buf, D_SUBTEXT);
    } else {
        disp_text(d, 4, y, "EOL: (need more data)", D_SUBTEXT);
    }
    y += 14;

    disp_hline(d, 0, y, ST7789_W, D_SUBTEXT); y += 5;
    disp_text(d, 4, y, "THIS SESSION:", D_ACCENT); y += 13;
    snprintf(buf, sizeof(buf), "Runtime: %.0fm", rs->session_h * 60.0f);
    disp_text(d, 4, y, buf, D_TEXT); y += 13;
    snprintf(buf, sizeof(buf), "Peak A: %.1f  W: %.0f",
             rs->session_peak_a, rs->session_peak_w);
    disp_text(d, 4, y, buf, D_TEXT);

    disp_footer(d, "3/3 predict", "Rot/Click=page");
}

static void __attribute__((unused)) _render_history_legacy(Display *d, const FullUiSnapshot *rs) {
    disp_fill(d, D_BG);
    disp_header(d, "LIFETIME STATS", NULL);
    switch (rs->hist_page) {
        case 0: _render_history_p0(d, rs); break;
        case 1: _render_history_p1_legacy(d, rs); break;
        case 2: _render_history_p2_legacy(d, rs); break;
        default: _render_history_p0(d, rs); break;
    }
    _badge_safe(d, rs);
}

static void _render_history_p1(Display *d, const FullUiSnapshot *rs) {
    char min_buf[16];
    char avg_buf[16];
    char max_buf[16];
    char peak_i_buf[16];
    char peak_p_buf[16];
    char alarms_buf[16];
    char ocp_buf[16];
    char otp_buf[16];
    uint16_t avg_col = (rs->temp_avg_c > 45.0f) ? D_RED :
                       (rs->temp_avg_c > 35.0f) ? D_ORANGE : D_GREEN;
    uint16_t max_col = (rs->temp_max_c > TEMP_BAT_CUT_C) ? D_RED :
                       (rs->temp_max_c > TEMP_BAT_WARN_C) ? D_ORANGE : D_TEXT;

    _draw_grid_background(d);
    _draw_screen_title(d, "LIFETIME STATS", "THERMAL");

    snprintf(min_buf, sizeof(min_buf), "%.0fC", rs->temp_min_c);
    snprintf(avg_buf, sizeof(avg_buf), "%.0fC", rs->temp_avg_c);
    snprintf(max_buf, sizeof(max_buf), "%.0fC", rs->temp_max_c);
    snprintf(peak_i_buf, sizeof(peak_i_buf), "%.1fA", rs->peak_current_a);
    snprintf(peak_p_buf, sizeof(peak_p_buf), "%.0fW", rs->peak_power_w);
    snprintf(alarms_buf, sizeof(alarms_buf), "%lu", (unsigned long)rs->total_alarm_events);
    snprintf(ocp_buf, sizeof(ocp_buf), "%lu", (unsigned long)rs->total_ocp_events);
    snprintf(otp_buf, sizeof(otp_buf), "%lu", (unsigned long)rs->total_temp_events);

    _draw_value_card(d, 12, 42, 68, 42, "MIN", min_buf, D_ACCENT, false, true);
    _draw_value_card(d, 86, 42, 68, 42, "AVG", avg_buf, avg_col, false, true);
    _draw_value_card(d, 160, 42, 68, 42, "MAX", max_buf, max_col, false, true);
    _draw_value_card(d, 12, 98, 104, 42, "PEAK I", peak_i_buf, D_ORANGE, false, true);
    _draw_value_card(d, 124, 98, 104, 42, "PEAK P", peak_p_buf, D_ORANGE, false, true);
    _draw_list_card(d, 12, 156, 216, 20, "ALARMS", alarms_buf,
                    rs->total_alarm_events ? D_RED : D_SUBTEXT, false);
    _draw_list_card(d, 12, 180, 216, 20, "OCP EVENTS", ocp_buf,
                    rs->total_ocp_events ? D_RED : D_SUBTEXT, false);
    _draw_list_card(d, 12, 204, 216, 20, "THERMAL EVENTS", otp_buf,
                    rs->total_temp_events ? D_ORANGE : D_SUBTEXT, false);
    _draw_list_card(d, 12, 228, 216, 20, "THERMAL STATE",
                    (rs->temp_max_c > TEMP_BAT_WARN_C) ? "WATCH" : "NOMINAL",
                    (rs->temp_max_c > TEMP_BAT_WARN_C) ? D_ORANGE : D_GREEN, false);

    _draw_footer_hint(d, "2/3 THERMAL", "ROT/CLICK");
}

static void _render_history_p2(Display *d, const FullUiSnapshot *rs) {
    char tte_buf[20];
    char cyc_buf[20];
    char eol_buf[20];
    char runtime_buf[16];
    char peak_a_buf[16];
    char peak_w_buf[16];
    const BatSnapshot *b = &rs->bat;
    float soh_deg = 100.0f - rs->soh_last;
    float cyc_per_pct = (soh_deg > 0.5f && rs->efc_total > 5.0f)
                        ? rs->efc_total / soh_deg : 10.0f;
    float soh_remain = rs->soh_last - 80.0f;
    int cyc_remain = (soh_remain > 0.0f) ? (int)(soh_remain * cyc_per_pct) : 0;
    uint16_t cyc_col = (cyc_remain > 500) ? D_GREEN : (cyc_remain > 200) ? D_ORANGE : D_RED;

    _draw_grid_background(d);
    _draw_screen_title(d, "LIFETIME STATS", "FORECAST");

    if (_has_runtime_estimate(b)) _format_runtime_short(b, tte_buf, sizeof(tte_buf));
    else snprintf(tte_buf, sizeof(tte_buf), "ESTIMATING");

    snprintf(cyc_buf, sizeof(cyc_buf), "~%d CYCLES", cyc_remain);

    if (rs->efc_total > 1.0f && rs->boot_count > 5) {
        float h_per_efc = (rs->session_h > 0.1f)
                          ? rs->session_h / rs->efc_total : 100.0f;
        float eol_h = cyc_remain * h_per_efc;
        if      (eol_h > 8760.0f) snprintf(eol_buf, sizeof(eol_buf), "~%.0f YR", eol_h / 8760.0f);
        else if (eol_h > 720.0f)  snprintf(eol_buf, sizeof(eol_buf), "~%.0f MO", eol_h / 720.0f);
        else                      snprintf(eol_buf, sizeof(eol_buf), "~%.0f HR", eol_h);
    } else {
        snprintf(eol_buf, sizeof(eol_buf), "NEEDS DATA");
    }

    snprintf(runtime_buf, sizeof(runtime_buf), "%.0fM", rs->session_h * 60.0f);
    snprintf(peak_a_buf, sizeof(peak_a_buf), "%.1fA", rs->session_peak_a);
    snprintf(peak_w_buf, sizeof(peak_w_buf), "%.0fW", rs->session_peak_w);

    _draw_value_card(d, 12, 42, 216, 42, "TIME TO EMPTY", tte_buf,
                     _has_runtime_estimate(b) ? _runtime_color(b) : D_SUBTEXT,
                     false, true);
    _draw_list_card(d, 12, 96, 216, 20, "REMAINING LIFE", cyc_buf, cyc_col, false);
    _draw_list_card(d, 12, 120, 216, 20, "END OF LIFE", eol_buf, D_SUBTEXT, false);
    _draw_value_card(d, 12, 156, 104, 42, "RUNTIME", runtime_buf, D_TEXT, false, true);
    _draw_value_card(d, 124, 156, 104, 42, "PEAK A", peak_a_buf, D_ACCENT, false, true);
    _draw_value_card(d, 12, 208, 216, 36, "PEAK W", peak_w_buf, D_ACCENT, false, true);

    _draw_footer_hint(d, "3/3 FORECAST", "ROT/CLICK");
}

static void _render_history(Display *d, const FullUiSnapshot *rs) {
    switch (rs->hist_page) {
        case 0: _render_history_p0(d, rs); break;
        case 1: _render_history_p1(d, rs); break;
        case 2: _render_history_p2(d, rs); break;
        default: _render_history_p0(d, rs); break;
    }
    _badge_safe(d, rs);
}

static void _render_i2c_scan(Display *d, const FullUiSnapshot *rs) {
    disp_header(d, "I2C SCAN", "Hold OK=back");
    if (!rs->scan_valid) { disp_text_center(d, 130, "No data", D_SUBTEXT); return; }
    int y = 24; bool found = false;
    for (int ch = 0; ch < 8 && y < ST7789_H - 20; ch++) {
        if (!rs->scan_counts[ch]) continue;
        found = true;
        char buf[24]; snprintf(buf, sizeof(buf), "CH%d:", ch);
        disp_text_safe(d, D_SAFE_LEFT, y, buf, D_ACCENT); y += 14;
        for (int j = 0; j < rs->scan_counts[ch] && y < ST7789_H-20; j++) {
            snprintf(buf, sizeof(buf), "  0x%02X", rs->scan_found[ch][j]);
            disp_text_safe(d, D_SAFE_LEFT, y, buf, D_TEXT); y += 14;
        }
    }
    if (!found) disp_text_center(d, 130, "No devices", D_ORANGE);
}

// P3.12: corrected splash — was "Click=bypass (unsafe)" which didn't work
static void _render_safe_splash(Display *d) {
    disp_fill(d, D_BG);
    disp_fill_rect(d, 0, 0, ST7789_W, 30, D_ORANGE);
    disp_text_center_safe(d, 8, "! STARTUP FAILED !", D_BG);
    disp_text_center_safe(d, 60, "Sensor validation failed.", D_ORANGE);
    disp_text_center_safe(d, 80, "All loads LOCKED.", D_TEXT);
    disp_text_center_safe(d, 100, "Check battery & sensors,", D_TEXT);
    disp_text_center_safe(d, 116, "then REBOOT.", D_TEXT);        // P3.12 fix
    disp_text_center_safe(d, 150, "Details on serial console.", D_SUBTEXT);
    // P3.12: removed "Click=bypass (unsafe)" — no bypass logic exists
    disp_text_center_safe(d, 175, "Reboot after fixing fault", D_SUBTEXT);
}

// ── Main render dispatcher (Core1) ────────────────────────────
static void _render_poweroff_splash(Display *d, const FullUiSnapshot *rs) {
    char buf[24];
    uint32_t remain_s = (rs->poweroff_remaining_ms + 999u) / 1000u;
    if (remain_s == 0) remain_s = 1;

    disp_fill(d, D_BG);
    disp_fill_rect(d, 0, 0, ST7789_W, 30, D_RED);
    disp_text_center_safe(d, 8, "POWER OFF", D_WHITE);
    _draw_icon(d, 102, 38, &UI_ICON_LOCK, D_ORANGE, 3);
    disp_text_center_safe(d, 58, "Keep holding OK", D_TEXT);
    disp_text_center_safe(d, 76, "Release to cancel", D_SUBTEXT);

    snprintf(buf, sizeof(buf), "%lus", (unsigned long)remain_s);
    disp_text2x(d, 88, 118, buf, D_ORANGE);

    disp_rect(d, 36, 172, 168, 18, D_SUBTEXT);
    int fill = (int)((168.0f * (float)(BTN_POWEROFF_MS - rs->poweroff_remaining_ms)) / (float)BTN_POWEROFF_MS);
    if (fill > 2) disp_fill_rect(d, 37, 173, fill - 2, 16, D_ORANGE);

    disp_text_center_safe(d, 206, "System will shut down", D_TEXT);
    disp_text_center_safe(d, 224, "Hold button until countdown ends", D_SUBTEXT);
}

void ui_render(UI *ui) {
    // Atomically check dirty + copy snapshot
    uint32_t save = spin_lock_blocking(ui->render_lock);
    if (!ui->dirty) { spin_unlock(ui->render_lock, save); return; }
    FullUiSnapshot rs;
    memcpy(&rs, &ui->rs, sizeof(rs));
    ui->dirty = false;
    spin_unlock(ui->render_lock, save);

    // Render from rs — NO live struct access beyond this point
    Display *d = ui->disp;
    disp_fill(d, D_BG);

    if (rs.poweroff_pending) {
        _render_poweroff_splash(d, &rs);
    } else if (rs.safe_mode && !rs.startup_ok && rs.state != S_DIAGNOSTICS) {
        _render_safe_splash(d);
    } else if (rs.screensaver_active) {
        _render_charging_saver(d, &rs);
    } else {
        switch (rs.state) {
            case S_MAIN:     _render_main_ref(d, &rs);        break;
            case S_STATS:    _render_stats_ref(d, &rs);       break;
            case S_BATTERY:  _render_battery_ref(d, &rs);     break;
            case S_DIAGNOSTICS: _render_diagnostics_ref(d, &rs); break;
            case S_PORTS:    _render_ports_ref(d, &rs);       break;
            case S_UI_CFG:   _render_ui_cfg_ref(d, &rs);      break;
            case S_ADVANCED: _render_advanced_ref(d, &rs);    break;
            case S_EVENTS:   _render_events(d, &rs);   break;
            case S_HISTORY:  _render_history(d, &rs);  break;
            case S_I2C_SCAN: _render_i2c_scan(d, &rs); break;
            default: break;
        }
    }

    if (rs.confirm_active) {
        _draw_confirm_overlay(d, &rs);
    }

    // Toast
    if (!rs.poweroff_pending && rs.toast_msg[0]) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((int32_t)(now - rs.toast_end_ms) < 0) {
            const char *lines[1] = {rs.toast_msg};
            disp_dialog(d, lines, 1);
            uint32_t s2 = spin_lock_blocking(ui->render_lock);
            ui->dirty = true;
            spin_unlock(ui->render_lock, s2);
        }
    }

    disp_flush(d);
}
