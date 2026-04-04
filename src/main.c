// ============================================================
// main.c -  PowerStation 3S2P В· RP2040 В· v5.0
//
// РџРѕСЃР»С–РґРѕРІРЅС–СЃС‚СЊ РІРІС–РјРєРЅРµРЅРЅСЏ:
//   1. pseq_latch()        -  relay GPIO safe OFF, bootstrap delay, GPIO_PWR_LATCH LOW в†’ MCU self-powered
//   2. _init_all()         -  РїРѕРІРЅР° С–РЅС–С†С–Р°Р»С–Р·Р°С†С–СЏ РїРµСЂРёС„РµСЂС–С—
//   3. _startup_validate() -  РїРµСЂРµРІС–СЂРєР° Р±Р°С‚Р°СЂРµС— (РЅР°РїСЂСѓРіР°, РєР»С–С‚РёРЅРё, РґР°С‚С‡РёРєРё)
//   4. pseq_resolve()      в†’ BootMode:
//        NORMAL       в†’ pwr_apply_policy(LOADS_ON)
//        CHARGE_ONLY  в†’ pwr_apply_policy(CHARGE_ONLY)  [SOC < 1%]
//        DIAGNOSTIC   в†’ safe mode + diagnostics screen
//
// Р’РёРјРєРЅРµРЅРЅСЏ:
//   - startup fail  в†’ BOOT_DIAGNOSTIC в†’ safe mode + diagnostics
//   - encoder 5s    в†’ pseq_user_poweroff()  [Р· ui_poll]
//
// FMEA-01: PowerPolicy; PORT_CHARGE РЅРµ РІРјРёРєР°С"С‚СЊСЃСЏ Р±РµР· charger_present
// FMEA-02: meas_valid/stale в†’ charge+inv РІРёРјРёРєР°С"С‚СЊСЃСЏ РїСЂРё Р·Р°СЃС‚Р°СЂС–Р»РёС… РґР°РЅРёС…
// FMEA-04: OCP РІРёРјРёРєР°С" РІСЃС– СЂРѕР·СЂСЏРґРЅС– РІРёС…РѕРґРё (protection.c)
// FMEA-15: PORT_FAN Р·Р°Р±Р»РѕРєРѕРІР°РЅРёР№ СЏРєС‰Рѕ lm75a_inv РІС–РґСЃСѓС‚РЅС–Р№
// ============================================================
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "config.h"
#include "drivers/tca9548a.h"
#include "drivers/ina226.h"
#include "drivers/ina3221.h"
#include "drivers/lm75a.h"
#include "drivers/st7789.h"
#include "bms/battery.h"
#include "bms/bms_logger.h"
#include "bms/bms_stats.h"
#include "bms/bms_ocv.h"
#include "app/power_control.h"
#include "app/power_sequencer.h"
#include "app/protection.h"
#include "app/buzzer.h"
#include "app/display.h"
#include "app/esp_manager.h"
#include "app/runtime_policy.h"
#include "app/save_manager.h"
#include "app/session_manager.h"
#include "app/system_settings.h"
#include "app/ui.h"

// в"Ђв"Ђ Globals в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
TCA9548A     g_tca;
INA226       g_ina_dis, g_ina_chg;
INA3221      g_ina3221;
LM75A        g_lm75a_bat, g_lm75a_inv;
Display      g_disp;
Battery      g_bat;
PowerControl g_pwr;
PowerSeq     g_pseq;
Protection   g_prot;
Buzzer       g_buz;
BmsLogger    g_logger;
BmsStats     g_stats;
EspManager   g_esp;
UI           g_ui;
SaveManager  g_save_mgr;
SessionManager g_session_mgr;

static BatSnapshot  g_bat_snapshot;
static spin_lock_t *g_bat_lock;
static uint         g_bat_lock_num;

static bool  g_inv_sensor_absent_logged = false;
static bool  g_fan_forced_on = false;
static volatile bool g_core1_ready = false;
static BootMode g_boot_mode = BOOT_NORMAL;

static absolute_time_t t_sensor, t_logic, t_save, t_loop;
static absolute_time_t t_ui;

static void _apply_runtime_settings(bool reconfigure_sensors);
static bool _esp_store_settings(const SystemSettings *next, bool reconfigure_sensors);

typedef struct {
    bool pack_valid;
    bool chg_valid;
    bool cells_valid;
    bool tbat_valid;
    bool tinv_valid;
    bool tinv_present;
    bool voltage_ok;
    bool cell_min_ok;
    bool delta_warn;
    bool startup_ok;
    float vmin;
    float soc_ocv;
} StartupDiagReport;

#if DEBUG_USB_BRINGUP
static void _relays_safe_init_for_usb_debug(void) {
    const uint8_t relays[] = {
        GPIO_DC_OUT, GPIO_USB_PD, GPIO_CHARGE_IN, GPIO_FAN, GPIO_PWR_LATCH
    };
    for (size_t i = 0; i < (sizeof(relays) / sizeof(relays[0])); ++i) {
        gpio_init(relays[i]);
        gpio_set_dir(relays[i], GPIO_OUT);
        gpio_put(relays[i], MOSFET_OFF);
    }
}
#endif

// в"Ђв"Ђ Helpers в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
static void _early_console_init(void) {
    stdio_init_all();
    if (USB_BOOT_LOG_WAIT_MS > 0) {
        uint32_t start_ms = to_ms_since_boot(get_absolute_time());
        while (!stdio_usb_connected()) {
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());
            if ((now_ms - start_ms) >= USB_BOOT_LOG_WAIT_MS) break;
            sleep_ms(10);
        }
    }
    sleep_ms(EARLY_CONSOLE_SETTLE_MS);
    printf("\n=== EARLY BOOT %s ===\n", FW_VERSION);
    printf("[DBG] stdio: USB CDC, ESP UART1 on GP%u/GP%u\n", ESP_UART_TX_PIN, ESP_UART_RX_PIN);
}

static void _snapshot_update(void) {
    uint32_t s = spin_lock_blocking(g_bat_lock);
    bat_snapshot(&g_bat, &g_bat_snapshot);
    spin_unlock(g_bat_lock, s);
}

static void _check_brownout(void) {
    log_set_brownout(g_bat.voltage < VBAT_BROWNOUT_V);
}

// в"Ђв"Ђ I2C recovery в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
static uint8_t g_i2c_fail_streak = 0;
static void _i2c_check_and_recover(bool ok) {
    if (ok) { g_i2c_fail_streak = 0; return; }
    if (++g_i2c_fail_streak >= RUNTIME_I2C_RECOVERY_STREAK) {
        printf("[I2C] bus recovery\n");
        i2c_bus_recover(I2C_PORT, I2C_SDA_PIN, I2C_SCL_PIN);
        g_i2c_fail_streak = 0;
        tca_init(&g_tca, I2C_PORT, TCA_ADDR);
        prot_set_i2c_fault(&g_prot, true);
    }
}

static void _log_i2c_scan(void) {
    uint8_t found[8][16] = {{0}};
    uint8_t counts[8] = {0};
    int total = tca_scan(&g_tca, found, counts);
    printf("[I2C] mux scan total=%d\n", total);
    for (int ch = 0; ch < 8; ++ch) {
        printf("[I2C] CH%d:", ch);
        if (counts[ch] == 0) {
            printf(" -");
        } else {
            for (int i = 0; i < counts[ch]; ++i) {
                printf(" 0x%02X", found[ch][i]);
            }
        }
        printf("\n");
    }
}

// FMEA-01: Charger present detection.
// Treat the charge input as present when INA226 reports more than 0.05 A.
static void _charger_detect(void) {
    bool chg_valid = bat_meas_fresh(&g_bat, MEAS_VALID_CHG);
    bool present = chg_valid && runtime_policy_charger_present(g_bat.i_chg);
    pwr_set_charger_present(&g_pwr, present);
}

// в"Ђв"Ђ FMEA-15: Fan relay guard в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
static void _guard_inverter(void) {
    if (!g_bat.inv_temp_sensor_ok) {
        pwr_disable(&g_pwr, PORT_FAN);
        if (!g_inv_sensor_absent_logged) {
            printf("[GUARD] FMEA-15: fan relay blocked (no temp sensor)\n");
            ui_toast(&g_ui, "Fan relay LOCKED: no temp sensor");
            g_inv_sensor_absent_logged = true;
        }
    }
}

static void _fan_control(void) {
    bool tbat_ok = bat_meas_fresh(&g_bat, MEAS_VALID_TBAT);
    bool tinv_ok = bat_meas_fresh(&g_bat, MEAS_VALID_TINV) && g_bat.inv_temp_sensor_ok;
    bool over_on = (tbat_ok && g_bat.temp_bat >= FAN_ON_TEMP_C) ||
                   (tinv_ok && g_bat.temp_inv >= FAN_ON_TEMP_C);
    bool below_off = (!tbat_ok || g_bat.temp_bat <= FAN_OFF_TEMP_C) &&
                     (!tinv_ok || g_bat.temp_inv <= FAN_OFF_TEMP_C);

    if (!g_bat.inv_temp_sensor_ok) {
        pwr_disable(&g_pwr, PORT_FAN);
        g_fan_forced_on = false;
        return;
    }

    if (!g_fan_forced_on && over_on) {
        pwr_enable(&g_pwr, PORT_FAN);
        g_fan_forced_on = true;
        printf("[THERM] fan ON: bat=%.1fC dc-usb=%.1fC\n", g_bat.temp_bat, g_bat.temp_inv);
    } else if (g_fan_forced_on && below_off) {
        pwr_disable(&g_pwr, PORT_FAN);
        g_fan_forced_on = false;
        printf("[THERM] fan OFF: bat=%.1fC dc-usb=%.1fC\n", g_bat.temp_bat, g_bat.temp_inv);
    }
}

// в"Ђв"Ђ Startup validation в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
static const char *_diag_status(bool ok, bool warn_only) {
    if (ok) return "OK";
    return warn_only ? "WARN" : "FAIL";
}

static uint16_t _diag_color(bool ok, bool warn_only) {
    if (ok) return D_GREEN;
    return warn_only ? D_ORANGE : D_RED;
}

static void _bootdiag_draw_progress(const char *title, const char *line1, const char *line2) {
    disp_fill(&g_disp, D_BG);
    disp_header(&g_disp, title, FW_VERSION);
    disp_text_center(&g_disp, 72, "Power on self-test", D_ACCENT);
    disp_text_center(&g_disp, 108, line1, D_TEXT);
    if (line2 && line2[0]) disp_text_center(&g_disp, 126, line2, D_SUBTEXT);
    disp_text_center(&g_disp, 214, "Checking sensors...", D_SUBTEXT);
    disp_flush_sync(&g_disp);
}

static void _bootdiag_draw_report(const StartupDiagReport *rep, uint32_t timeout_left_ms) {
    char buf[48];
    int y = 22;
    disp_fill(&g_disp, D_BG);
    disp_header(&g_disp, "BOOT DIAGNOSTICS", FW_VERSION);

    if (rep->startup_ok) {
        disp_fill_rect(&g_disp, 0, 20, ST7789_W, 14, 0x0140u);
        disp_text_center(&g_disp, 23, "Startup checks passed", D_WHITE);
    } else {
        disp_fill_rect(&g_disp, 0, 20, ST7789_W, 14, D_RED);
        disp_text_center(&g_disp, 23, "Startup blocked - safe mode", D_WHITE);
    }

    y = 42;
    struct {
        const char *label;
        bool ok;
        bool warn_only;
    } checks[] = {
        {"PACK INA226 ", rep->pack_valid, false},
        {"CHG  INA226 ", rep->chg_valid, false},
        {"CELL INA3221", rep->cells_valid, false},
        {"TBAT LM75   ", rep->tbat_valid, true},
        {"TUSB LM75   ", rep->tinv_present && rep->tinv_valid, true},
    };

    for (int i = 0; i < 5; i++, y += 16) {
        disp_text(&g_disp, 4, y, checks[i].label, D_TEXT);
        disp_text_right(&g_disp, y,
                        _diag_status(checks[i].ok, checks[i].warn_only),
                        _diag_color(checks[i].ok, checks[i].warn_only));
    }

    y += 4;
    disp_hline(&g_disp, 0, y, ST7789_W, D_SUBTEXT);
    y += 8;

    snprintf(buf, sizeof(buf), "VBAT %.2fV  CELL %.3fV", g_bat.voltage, rep->vmin);
    disp_text(&g_disp, 4, y, buf, rep->voltage_ok && rep->cell_min_ok ? D_ACCENT : D_RED); y += 16;
    snprintf(buf, sizeof(buf), "dV %.0fmV  SOC %.1f%%", g_bat.delta_mv, rep->soc_ocv);
    disp_text(&g_disp, 4, y, buf, rep->delta_warn ? D_ORANGE : D_TEXT); y += 16;
    snprintf(buf, sizeof(buf), "TBAT %.1fC  TUSB %.1fC", g_bat.temp_bat, g_bat.temp_inv);
    disp_text(&g_disp, 4, y, buf, D_TEXT); y += 16;
    snprintf(buf, sizeof(buf), "IDIS %.1fA  ICHG %.1fA", g_bat.i_dis, g_bat.i_chg);
    disp_text(&g_disp, 4, y, buf, D_TEXT);

    if (rep->startup_ok) {
        snprintf(buf, sizeof(buf), "OK=run DOWN=diag %lus", (unsigned long)(timeout_left_ms / 1000u));
    } else {
        snprintf(buf, sizeof(buf), "OK=diag auto %lus", (unsigned long)(timeout_left_ms / 1000u));
    }
    disp_footer(&g_disp, buf, NULL);
    disp_flush_sync(&g_disp);
}

static bool _bootdiag_wait_confirm(const StartupDiagReport *rep, bool *force_diag_ui) {
    uint32_t confirm_ms = rep->startup_ok ? 3000u : BOOT_DIAG_CONFIRM_MS;
    uint32_t start_ms = to_ms_since_boot(get_absolute_time());
    bool ok_prev = false;
    bool dn_prev = false;

    while (1) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        uint32_t elapsed = now_ms - start_ms;
        uint32_t timeout_left = (elapsed >= confirm_ms) ? 0u : (confirm_ms - elapsed);
        _bootdiag_draw_report(rep, timeout_left);

        bool ok_now = (gpio_get(BTN_OK_PIN) == 0);
        bool dn_now = (gpio_get(BTN_DOWN_PIN) == 0);
        if (ok_now && !ok_prev) {
            *force_diag_ui = !rep->startup_ok;
            return true;
        }
        if (dn_now && !dn_prev) {
            *force_diag_ui = true;
            return true;
        }
        if (elapsed >= confirm_ms) {
            *force_diag_ui = !rep->startup_ok;
            return true;
        }

        ok_prev = ok_now;
        dn_prev = dn_now;
        sleep_ms(40);
    }
}

static bool _startup_validate(StartupDiagReport *rep) {
    printf("[INIT] startup validation...\n");
    memset(rep, 0, sizeof(*rep));
    _bootdiag_draw_progress("BOOT DIAGNOSTICS", "Reading sensors", "Please wait");
    bool sample_ok = false;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        bat_read_sensors(&g_bat);

        bool pack_valid  = (g_bat.meas_valid & MEAS_VALID_PACK)  != 0;
        bool cells_valid = (g_bat.meas_valid & MEAS_VALID_CELLS) != 0;
        bool voltage_ok  = (g_bat.voltage >= VBAT_MIN_BOOT_V && g_bat.voltage <= 15.0f);

        if (pack_valid && cells_valid && voltage_ok) {
            sample_ok = true;
            if (attempt > 1) {
                printf("[INIT] startup validation recovered on attempt %d\n", attempt);
            }
            break;
        }

        printf("[INIT] startup sample %d/5 incomplete: meas=0x%02X V=%.3fV\n",
               attempt, g_bat.meas_valid, g_bat.voltage);

        if (attempt < 5) {
            i2c_bus_recover(I2C_PORT, I2C_SDA_PIN, I2C_SCL_PIN);
            tca_init(&g_tca, I2C_PORT, TCA_ADDR);
            sleep_ms(80);
        }
    }

    if (!sample_ok) {
        printf("[INIT] startup validation continuing with degraded sensor set\n");
    }

    rep->pack_valid   = (g_bat.meas_valid & MEAS_VALID_PACK)  != 0;
    rep->chg_valid    = (g_bat.meas_valid & MEAS_VALID_CHG)   != 0;
    rep->cells_valid  = (g_bat.meas_valid & MEAS_VALID_CELLS) != 0;
    rep->tbat_valid   = (g_bat.meas_valid & MEAS_VALID_TBAT)  != 0;
    rep->tinv_valid   = (g_bat.meas_valid & MEAS_VALID_TINV)  != 0;
    rep->tinv_present = g_bat.inv_temp_sensor_ok;
    rep->voltage_ok   = (g_bat.voltage >= VBAT_MIN_BOOT_V && g_bat.voltage <= 15.0f);
    if (!rep->voltage_ok)
        printf("[INIT] FAIL: pack V=%.3fV\n", g_bat.voltage);

    float vmin = g_bat.v_b1;
    if (g_bat.v_b2 < vmin) vmin = g_bat.v_b2;
    if (g_bat.v_b3 < vmin) vmin = g_bat.v_b3;
    rep->vmin = vmin;
    rep->cell_min_ok = (vmin >= 3.0f);
    if (!rep->cell_min_ok) {
        printf("[INIT] FAIL: cell V=%.3fV\n", vmin);
    }

    if (!rep->cells_valid) {
        printf("[INIT] FAIL: cell measurement invalid\n");
    }
    if (!rep->tbat_valid)
        printf("[INIT] WARN: battery temp sensor failed\n");
    if (!rep->tinv_present)
        printf("[INIT] WARN: dc-usb temp sensor absent\n");
    else if (!rep->tinv_valid)
        printf("[INIT] WARN: dc-usb temp sensor failed\n");
    rep->delta_warn = (g_bat.delta_mv > 300.0f);
    if (rep->delta_warn)
        printf("[INIT] WARN: cell delta=%.0fmV\n", g_bat.delta_mv);

    rep->soc_ocv = bms_ocv_pack_to_soc(g_bat.voltage, g_bat.temp_bat) * 100.0f;
    rep->startup_ok = rep->voltage_ok && rep->cell_min_ok && rep->cells_valid && rep->pack_valid;
    printf("[INIT] %s: V=%.3fV SOC=%.1f%% T=%.1fC dV=%.0fmV\n",
           rep->startup_ok ? "OK" : "FAIL",
           g_bat.voltage, rep->soc_ocv, g_bat.temp_bat, g_bat.delta_mv);
    return rep->startup_ok;
}

// в"Ђв"Ђ Core1 в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
static void core1_entry(void) {
    multicore_lockout_victim_init();
    g_core1_ready = true;
    absolute_time_t t_disp = get_absolute_time();
    while (1) {
        t_disp = delayed_by_ms(t_disp, DISPLAY_MS);
        sleep_until(t_disp);
        disp_flush_wait(&g_disp);
        ui_render(&g_ui);
    }
}

// в"Ђв"Ђ Hardware init в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
static void _init_i2c(void) {
    i2c_init(I2C_PORT, I2C_FREQ_HZ);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
}

static bool _init_sensors(bool *inv_ok_out) {
    const SystemSettings *cfg = settings_get();
    tca_init(&g_tca, I2C_PORT, TCA_ADDR);
    _log_i2c_scan();
    bool ok = true;
    if (!ina226_init(&g_ina_dis, I2C_PORT, &g_tca,
                     TCA_CH_DIS, INA226_DIS_ADDR, cfg->shunt_dis_mohm, IMAX_DIS_A))
        { printf("[INIT] INA226 DIS fail\n"); ok = false; }
    if (!ina226_init(&g_ina_chg, I2C_PORT, &g_tca,
                     TCA_CH_CHG, INA226_CHG_ADDR, cfg->shunt_chg_mohm, IMAX_CHG_A))
        { printf("[INIT] INA226 CHG fail\n"); ok = false; }
    if (!ina3221_init(&g_ina3221, I2C_PORT, &g_tca,
                      TCA_CH_INA3221, INA3221_ADDR))
        { printf("[INIT] INA3221 fail\n"); ok = false; }
    if (!lm75a_init(&g_lm75a_bat, I2C_PORT, &g_tca,
                    TCA_CH_LM75A_BAT, LM75A_BAT_ADDR,
                    cfg->temp_bat_cut_c, cfg->temp_bat_warn_c))
        { printf("[INIT] LM75A bat fail\n"); ok = false; }

    // FMEA-15: DC-USB temp -  optional, absence blocks PORT_FAN
    *inv_ok_out = lm75a_init(&g_lm75a_inv, I2C_PORT, &g_tca,
                              TCA_CH_LM75A_INV, LM75A_INV_ADDR,
                              cfg->temp_inv_cut_c, cfg->temp_inv_warn_c);
    if (!*inv_ok_out)
        printf("[INIT] LM75A dc-usb absent -  fan relay will be blocked\n");

    return ok;
}

static void _init_all(void) {
    sleep_ms(PWR_LATCH_SETTLE_MS);
    printf("\n=== PowerStation BMS %s ===\n", FW_VERSION);

    settings_init();

    g_bat_lock_num = spin_lock_claim_unused(true);
    g_bat_lock     = spin_lock_instance(g_bat_lock_num);

    pwr_init(&g_pwr);
    buz_init(&g_buz, BUZZER_PIN, true);
    printf("[INIT] ST7789 SPI0 SCK=%u MOSI=%u DC=%u CS=%u RST=%u @ %uHz\n",
           SPI_SCK_PIN, SPI_MOSI_PIN, LCD_DC_PIN, SPI_CS_PIN, LCD_RST_PIN, SPI_BAUD_HZ);
    disp_init(&g_disp);
    _bootdiag_draw_progress("BOOT DIAGNOSTICS", "Display ready", "Init I2C and sensors");
    _init_i2c();

    bool inv_ok;
    _init_sensors(&inv_ok);

    bat_init(&g_bat,
             &g_ina_dis, &g_ina_chg, &g_ina3221,
             &g_lm75a_bat, inv_ok ? &g_lm75a_inv : NULL,
             &g_pwr);

    prot_init(&g_prot, &g_pwr);
    log_init(&g_logger);
    stats_init(&g_stats);
    esp_init(&g_esp, &g_logger, &g_stats, _esp_store_settings);
    bat_seed_predictor(&g_bat,
                       stats_predictor_baseline_power_w(&g_stats),
                       stats_predictor_peukert(&g_stats));

    ui_init(&g_ui, &g_disp, &g_pwr, &g_pseq,
            &g_buz, &g_esp, &g_bat_snapshot, &g_prot, &g_stats, &g_logger,
            _apply_runtime_settings);
}

static bool _esp_store_settings(const SystemSettings *next, bool reconfigure_sensors) {
    if (!settings_store(next)) return false;
    _apply_runtime_settings(reconfigure_sensors);
    return true;
}

static void _apply_runtime_settings(bool reconfigure_sensors) {
    const SystemSettings *cfg = settings_get();

    if (reconfigure_sensors) {
        if (!ina226_init(&g_ina_dis, I2C_PORT, &g_tca,
                         TCA_CH_DIS, INA226_DIS_ADDR,
                         cfg->shunt_dis_mohm, IMAX_DIS_A)) {
            printf("[SET] reconfig INA226 DIS failed\n");
        }
        if (!ina226_init(&g_ina_chg, I2C_PORT, &g_tca,
                         TCA_CH_CHG, INA226_CHG_ADDR,
                         cfg->shunt_chg_mohm, IMAX_CHG_A)) {
            printf("[SET] reconfig INA226 CHG failed\n");
        }
        if (!lm75a_init(&g_lm75a_bat, I2C_PORT, &g_tca,
                        TCA_CH_LM75A_BAT, LM75A_BAT_ADDR,
                        cfg->temp_bat_cut_c, cfg->temp_bat_warn_c)) {
            printf("[SET] reconfig LM75A BAT failed\n");
        }
        if (g_bat.inv_temp_sensor_ok &&
            !lm75a_init(&g_lm75a_inv, I2C_PORT, &g_tca,
                        TCA_CH_LM75A_INV, LM75A_INV_ADDR,
                        cfg->temp_inv_cut_c, cfg->temp_inv_warn_c)) {
            printf("[SET] reconfig LM75A INV failed\n");
        }
    }

    bat_apply_settings(&g_bat, cfg->capacity_ah);
    bat_seed_predictor(&g_bat,
                       stats_predictor_baseline_power_w(&g_stats),
                       stats_predictor_peukert(&g_stats));
    esp_apply_settings(&g_esp, cfg);
    _snapshot_update();
}

static void _apply_runtime_esp_settings(void) {
    SystemSettings runtime_cfg;
    settings_copy(&runtime_cfg);
    if (g_boot_mode == BOOT_OTA_SAFE) {
        runtime_cfg.esp_mode = ESP_MODE_OTA;
    }
    esp_apply_settings(&g_esp, &runtime_cfg);
}

// в"Ђв"Ђ main()в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
int main(void) {
    // в"Ђв"Ђ 1. LATCH / USB debug bring-up в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
#if DEBUG_USB_BRINGUP
    _relays_safe_init_for_usb_debug();
#else
    // First instruction: keep relays OFF, wait bootstrap delay, then latch SYSTEM_HOLD.
    pseq_latch(&g_pseq);
#endif
    _early_console_init();
#if DEBUG_USB_BRINGUP
    printf("[SEQ] DEBUG_USB_BRINGUP=1: pseq_latch skipped, relays forced OFF\n");
#else
    printf("[SEQ] SYSTEM_HOLD asserted on GP%u after %ums bootstrap delay\n",
           GPIO_PWR_LATCH, PWR_BOOTSTRAP_DELAY_MS);
#endif

    // в"Ђв"Ђ 2. INIT в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
    _init_all();

    // в"Ђв"Ђ 3. VALIDATION в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
    StartupDiagReport diag = {0};
    bool startup_ok = _startup_validate(&diag);
    bool force_diag_ui = false;
    _bootdiag_wait_confirm(&diag, &force_diag_ui);
    float soc_ocv   = diag.soc_ocv;
    const SystemSettings *boot_cfg = settings_get();
    BootMode mode   = pseq_resolve(&g_pseq, startup_ok, soc_ocv,
                                   boot_cfg->pico_mode == PICO_MODE_OTA_SAFE);
    g_boot_mode = mode;

    _snapshot_update();
    _charger_detect();
    ui_set_startup_ok(&g_ui, startup_ok);
    if (!startup_ok || force_diag_ui)
        ui_set_state(&g_ui, S_DIAGNOSTICS);

    // в"Ђв"Ђ 4. APPLY BOOT POLICY в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
    switch (mode) {

    case BOOT_NORMAL:
        pwr_apply_policy(&g_pwr, PWR_POLICY_LOADS_ON);
        _guard_inverter();
        _fan_control();
        buz_play(&g_buz, BUZ_BOOT);
        printf("[BOOT] NORMAL chg:%s\n",
               pwr_is_charger_present(&g_pwr) ? "yes" : "no");
        break;

    case BOOT_CHARGE_ONLY:
        pwr_apply_policy(&g_pwr, PWR_POLICY_CHARGE_ONLY);
        _fan_control();
        ui_toast(&g_ui, "Low SOC -  charge only");
        buz_play(&g_buz, BUZ_ALARM_WARN);
        printf("[BOOT] CHARGE_ONLY\n");
        break;

    case BOOT_DIAGNOSTIC:
        pwr_apply_policy(&g_pwr, PWR_POLICY_ISOLATED);
        pwr_set_safe_mode(&g_pwr, true);
        buz_play(&g_buz, BUZ_ALARM_CRIT);
        ui_toast(&g_ui, "Startup blocked - diagnostics");
        printf("[BOOT] DIAGNOSTIC - validation failed\n");
        printf("[BOOT] DIAGNOSTIC - safe mode active\n");
        // Р—Р°РїСѓСЃРєР°С"РјРѕ Core1 С‰РѕР± РїРѕРєР°Р·Р°С‚Рё РїРѕРјРёР»РєСѓ РЅР° РґРёСЃРїР»РµС—
        // Diagnostics mode keeps UI alive; Core1 starts once below.
        // pseq_self_off disabled: keep diagnostics screen available.
        // РќРµ РїРѕРІРµСЂС‚Р°С"С‚СЊСЃСЏ
        break;

    case BOOT_OTA_SAFE:
        pwr_apply_policy(&g_pwr, PWR_POLICY_ISOLATED);
        pwr_set_safe_mode(&g_pwr, true);
        ui_set_state(&g_ui, S_OTA);
        buz_play(&g_buz, BUZ_BOOT);
        printf("[BOOT] OTA_SAFE - loads isolated, ESP forced OTA\n");
        break;
    }

    esp_set_pico_ota_ready(&g_esp, mode == BOOT_OTA_SAFE);
    esp_set_boot_ready(&g_esp, mode != BOOT_DIAGNOSTIC);
    _apply_runtime_esp_settings();
    _snapshot_update();

    absolute_time_t now = get_absolute_time();
    save_manager_init(&g_save_mgr, &g_bat, to_ms_since_boot(now));
    session_manager_init(&g_session_mgr);
    t_sensor = delayed_by_ms(now, SENSOR_MS);
    t_logic  = delayed_by_ms(now, LOGIC_MS);
    t_save   = delayed_by_ms(now, SAVE_MS);
    t_ui     = delayed_by_ms(now, UI_REFRESH_MS);
    t_loop   = delayed_by_ms(now, 1);

    watchdog_enable(8000, 1);
    multicore_launch_core1(core1_entry);
    absolute_time_t core1_deadline = delayed_by_ms(get_absolute_time(), 100);
    while (!g_core1_ready && absolute_time_diff_us(get_absolute_time(), core1_deadline) > 0) {
        tight_loop_contents();
    }
    if (g_core1_ready && g_bat.soh_est.migration_pending) {
        printf("[SOH] committing deferred v3->v4 migration\n");
        bat_save(&g_bat);
    }
    if (g_core1_ready) {
        log_boot(&g_logger, g_bat.soc, g_bat.voltage);
    } else {
        printf("[BOOT] core1 not ready, boot log skipped\n");
    }
    printf("[BOOT] done\n");

    // -- Minimal OTA loop (no sensors/BMS/protection/fan/inverter) --
    if (mode == BOOT_OTA_SAFE) {
        printf("[OTA] entering minimal OTA loop\n");
        while (1) {
            watchdog_update();
            absolute_time_t tnow = get_absolute_time();
            uint32_t ms_now = to_ms_since_boot(tnow);

            esp_update(&g_esp, NULL, ms_now);

            while (absolute_time_diff_us(tnow, t_ui) <= 0) {
                t_ui = delayed_by_ms(t_ui, UI_REFRESH_MS);
                ui_refresh(&g_ui);
            }
            ui_poll(&g_ui);
            buz_tick(&g_buz);

            t_loop = delayed_by_ms(t_loop, 1);
            sleep_until(t_loop);
        }
    }

    // -- Core0 main loop ------------------------------------------
    static uint32_t ms_prev          = 0;
    static uint32_t prev_alarms      = 0;
    static uint32_t charger_check_ms = 0;

    while (1) {
        watchdog_update();
        t_loop = delayed_by_ms(t_loop, 1);
        sleep_until(t_loop);
        absolute_time_t tnow = get_absolute_time();

        // Sensors
        while (absolute_time_diff_us(tnow, t_sensor) <= 0) {
            t_sensor = delayed_by_ms(t_sensor, SENSOR_MS);
            bat_read_sensors(&g_bat);
            _charger_detect();
            _i2c_check_and_recover(g_bat.i2c_fail_count == 0);
            _check_brownout();
            _snapshot_update();
        }

        // Logic
        while (absolute_time_diff_us(tnow, t_logic) <= 0) {
            uint32_t ms_now = to_ms_since_boot(tnow);
            float dt_s = (ms_prev > 0)
                         ? (float)(ms_now - ms_prev) * 0.001f
                         : LOGIC_MS * 0.001f;
            if (dt_s > 1.0f) dt_s = LOGIC_MS * 0.001f;
            ms_prev = ms_now;
            t_logic = delayed_by_ms(t_logic, LOGIC_MS);

            bat_update_bms(&g_bat, dt_s);
            save_manager_update_guard(&g_save_mgr, &g_bat, ms_now);

            BatSnapshot snap;
            uint32_t sv = spin_lock_blocking(g_bat_lock);
            bat_snapshot(&g_bat, &snap);
            spin_unlock(g_bat_lock, sv);

            session_manager_step(&g_session_mgr, &g_bat, &snap, &g_logger, &g_stats, ms_now);

            prot_check(&g_prot, &g_bat);

            uint32_t new_alarms = g_prot.alarms & ~prev_alarms;
            prev_alarms = g_prot.alarms;

            if (new_alarms) {
                if (new_alarms & (ALARM_TEMP_CUT | ALARM_INV_CUT)) {
                    buz_play(&g_buz, BUZ_TEMP_CUT);
                } else if (new_alarms & (ALARM_TEMP_SAFE | ALARM_INV_SAFE)) {
                    buz_play(&g_buz, BUZ_ALARM_CRIT);
                } else if (new_alarms & ALARM_TEMP_BUZZ) {
                    buz_play(&g_buz, BUZ_TEMP_WARN);
                } else if (prot_has_critical(&g_prot)) {
                    buz_play(&g_buz, BUZ_ALARM_CRIT);
                } else if (prot_has_warning(&g_prot)) {
                    buz_play(&g_buz, BUZ_ALARM_WARN);
                }
                log_alarm(&g_logger, g_prot.alarms,
                          snap.soc, snap.voltage, snap.temp_bat);
            }

            stats_update(&g_stats,
                         g_bat.soh_est.dis_wh_total,
                         g_bat.soh_est.chg_wh_total,
                         g_bat.efc, g_bat.temp_bat,
                         g_bat.i_dis, g_bat.power_w,
                         g_bat.soh, new_alarms);
            esp_update(&g_esp, &snap, ms_now);

            // FMEA-01: re-check charger presence РєРѕР¶РЅС– 5s
            if ((ms_now - charger_check_ms) >= RUNTIME_CHARGER_RECHECK_MS) {
                charger_check_ms = ms_now;
                _charger_detect();
            }

            _guard_inverter();
            _fan_control();
            _snapshot_update();
        }

        while (absolute_time_diff_us(tnow, t_ui) <= 0) {
            t_ui = delayed_by_ms(t_ui, UI_REFRESH_MS);
            ui_refresh(&g_ui);
        }

        ui_poll(&g_ui);

        // Flash save
        bool time_save = (absolute_time_diff_us(tnow, t_save) <= 0);
        uint32_t save_now_ms = to_ms_since_boot(tnow);
        if (time_save || save_manager_should_save(&g_save_mgr, &g_bat, save_now_ms)) {
            if (time_save) t_save = delayed_by_ms(t_save, SAVE_MS);
            save_manager_commit(&g_save_mgr, &g_bat, &g_logger, &g_stats, save_now_ms);
        }

        buz_tick(&g_buz);
    }

    return 0;
}
