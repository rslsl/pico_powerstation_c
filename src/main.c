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
#include "pico/bootrom.h"
#include "pico/stdio_usb.h"
#include "hardware/i2c.h"
#include "hardware/address_mapped.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
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
#include "app/boot_control.h"
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

#ifndef POWERSTATION_RECOVERY_SLOT
#define POWERSTATION_RECOVERY_SLOT 0
#endif

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
static bool  g_fan_blocked_logged = false;
static bool  g_prev_brownout = false;
static bool  g_prev_charger_present = false;
static uint8_t g_prev_meas_valid = 0;
static volatile bool g_core1_ready = false;
static BootMode g_boot_mode = BOOT_NORMAL;
static bool g_boot_ota_safe_requested = false;
#if POWERSTATION_RECOVERY_SLOT
static bool g_recovery_boot_menu_requested = false;
#endif

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

static void _boot_buttons_init(void) {
    gpio_init(BTN_UP_PIN);
    gpio_set_dir(BTN_UP_PIN, GPIO_IN);
    gpio_pull_up(BTN_UP_PIN);

    gpio_init(BTN_OK_PIN);
    gpio_set_dir(BTN_OK_PIN, GPIO_IN);
    gpio_pull_up(BTN_OK_PIN);

    gpio_init(BTN_DOWN_PIN);
    gpio_set_dir(BTN_DOWN_PIN, GPIO_IN);
    gpio_pull_up(BTN_DOWN_PIN);
}

static BootSlot _compiled_boot_slot(void) {
#if PICO_OTA_CURRENT_SLOT == 1
    return BOOT_SLOT_A;
#elif PICO_OTA_CURRENT_SLOT == 2
    return BOOT_SLOT_B;
#else
    return BOOT_SLOT_NONE;
#endif
}

static void _maybe_confirm_running_slot(void) {
    BootControlState state;
    BootSlot slot = _compiled_boot_slot();
    bool loaded;
    bool changed = false;

    if (slot == BOOT_SLOT_NONE) return;

    loaded = bootctl_load(&state);
    if (slot == BOOT_SLOT_A && loaded) {
        return;
    }
    if (!loaded) {
        bootctl_defaults(&state);
    }

    if (!bootctl_slot_has_bootable_image(&state, slot)) {
        extern uint8_t __flash_binary_start;
        extern uint8_t __flash_binary_end;
        uint32_t image_size = (uint32_t)(&__flash_binary_end - &__flash_binary_start);

        if (!bootctl_stage_update(&state, slot, image_size, 0u, FW_VERSION)) {
            printf("[BOOT] failed to stage running slot metadata for %s\n",
                   slot == BOOT_SLOT_A ? "slot-a" : "slot-b");
            return;
        }
        changed = true;
    }

    if (state.pending_slot == (uint8_t)slot || state.confirmed_slot != (uint8_t)slot) {
        if (!bootctl_mark_confirmed(&state, slot)) {
            printf("[BOOT] failed to confirm %s\n",
                   slot == BOOT_SLOT_A ? "slot-a" : "slot-b");
            return;
        }
        changed = true;
    }

    if (changed) {
        if (bootctl_store(&state)) {
            printf("[BOOT] confirmed %s as bootable\n",
                   slot == BOOT_SLOT_A ? "slot-a" : "slot-b");
        } else {
            printf("[BOOT] failed to store boot metadata for %s\n",
                   slot == BOOT_SLOT_A ? "slot-a" : "slot-b");
        }
    }
}

#if POWERSTATION_RECOVERY_SLOT
typedef enum {
    RECOVERY_BOOT_MENU_OTA = 0,
    RECOVERY_BOOT_MENU_MAIN = 1,
    RECOVERY_BOOT_MENU_USB = 2,
    RECOVERY_BOOT_MENU_COUNT
} RecoveryBootMenuChoice;

static bool _consume_recovery_boot_menu_request(void) {
    bool requested = watchdog_hw->scratch[BOOTCTL_RECOVERY_MENU_SCRATCH] == BOOTCTL_RECOVERY_MENU_MAGIC;
    watchdog_hw->scratch[BOOTCTL_RECOVERY_MENU_SCRATCH] = 0u;
    return requested;
}

static bool _slot_vectors_valid_local(BootSlot slot) {
    const BootSlotRegion *region = bootctl_slot_region(slot);
    uint32_t vector_addr;
    uint32_t sp;
    uint32_t pc;
    uint32_t pc_min;
    uint32_t pc_max;

    if (!region) return false;

    vector_addr = XIP_BASE + region->flash_offset + 0x100u;
    sp = *(const uint32_t *)(uintptr_t)vector_addr;
    pc = *(const uint32_t *)(uintptr_t)(vector_addr + 4u);
    pc_min = XIP_BASE + region->flash_offset + 0x100u;
    pc_max = XIP_BASE + region->flash_offset + region->max_size;

    if (sp < 0x20000000u || sp > 0x20042000u || ((sp & 0x3u) != 0u)) return false;
    if ((pc & 0x1u) == 0u || pc < pc_min || pc >= pc_max) return false;
    return true;
}

static void _draw_recovery_boot_menu(int selected, bool main_available, const char *status_text, uint16_t status_col) {
    static const char *k_titles[RECOVERY_BOOT_MENU_COUNT] = {
        "PICO OTA",
        "BOOT MAIN",
        "USB BOOTSEL",
    };
    static const char *k_notes[RECOVERY_BOOT_MENU_COUNT] = {
        "Stay in recovery and wait for WiFi upload",
        "Reboot through loader into main slot B",
        "Enter RP2040 USB flashing mode",
    };

    disp_fill(&g_disp, D_BG);
    disp_header(&g_disp, "BOOT MENU", "RECOVERY");
    disp_text_center_safe(&g_disp, 34, "DOWN HELD ON POWER-ON", D_SUBTEXT);

    for (int i = 0; i < RECOVERY_BOOT_MENU_COUNT; ++i) {
        const int y = 56 + i * 54;
        const bool active = (i == selected);
        const bool enabled = (i != RECOVERY_BOOT_MENU_MAIN) || main_available;
        const uint16_t frame_col = active ? D_ACCENT : D_GRAY;
        const uint16_t fill_col = active ? 0x1147u : 0x08A6u;
        const uint16_t text_col = enabled ? (active ? D_WHITE : D_TEXT) : D_SUBTEXT;

        disp_fill_rect(&g_disp, 12, y, 216, 42, fill_col);
        disp_rect(&g_disp, 12, y, 216, 42, frame_col);
        disp_text(&g_disp, 20, y + 8, k_titles[i], text_col);
        disp_text(&g_disp, 20, y + 22, k_notes[i], enabled ? D_SUBTEXT : D_RED);
    }

    if (status_text && status_text[0]) {
        disp_fill_rect(&g_disp, 12, 220, 216, 22, 0x08A6u);
        disp_rect(&g_disp, 12, 220, 216, 22, status_col);
        disp_text_center_safe(&g_disp, 227, status_text, status_col);
    }

    disp_footer(&g_disp, "UP/DN=SELECT", "OK=START");
    disp_flush_sync(&g_disp);
}

static void _reboot_to_requested_slot(BootSlot slot) {
    watchdog_hw->scratch[BOOTCTL_WATCHDOG_HANDOFF_MAGIC_SCRATCH] = BOOTCTL_WATCHDOG_HANDOFF_MAGIC;
    watchdog_hw->scratch[BOOTCTL_WATCHDOG_HANDOFF_SLOT_SCRATCH] = (uint32_t)slot;
    sleep_ms(20);
    watchdog_reboot(0u, 0u, 50u);
    while (1) {
        tight_loop_contents();
    }
}

static RecoveryBootMenuChoice _run_recovery_boot_menu(void) {
    const uint8_t pins[3] = { BTN_UP_PIN, BTN_OK_PIN, BTN_DOWN_PIN };
    bool prev[3] = { false, false, false };
    uint32_t last_ms[3] = { 0u, 0u, 0u };
    int selected = RECOVERY_BOOT_MENU_OTA;
    bool dirty = true;
    bool main_available = _slot_vectors_valid_local(BOOT_SLOT_B);
    char status_text[40];
    uint16_t status_col = D_SUBTEXT;

    snprintf(status_text, sizeof(status_text),
             "%s",
             main_available ? "SELECT RECOVERY ACTION" : "MAIN SLOT B NOT AVAILABLE");

    while (1) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        if (dirty) {
            _draw_recovery_boot_menu(selected, main_available, status_text, status_col);
            dirty = false;
        }

        for (int i = 0; i < 3; ++i) {
            bool pressed = (gpio_get(pins[i]) == 0);
            if (pressed != prev[i] && (now - last_ms[i]) >= BTN_DEBOUNCE_MS) {
                prev[i] = pressed;
                last_ms[i] = now;
                if (!pressed) continue;

                if (i == 0) {
                    selected = (selected + RECOVERY_BOOT_MENU_COUNT - 1) % RECOVERY_BOOT_MENU_COUNT;
                    snprintf(status_text, sizeof(status_text), "%s", "SELECT RECOVERY ACTION");
                    status_col = D_SUBTEXT;
                    dirty = true;
                } else if (i == 2) {
                    selected = (selected + 1) % RECOVERY_BOOT_MENU_COUNT;
                    snprintf(status_text, sizeof(status_text), "%s", "SELECT RECOVERY ACTION");
                    status_col = D_SUBTEXT;
                    dirty = true;
                } else {
                    if (selected == RECOVERY_BOOT_MENU_MAIN && !main_available) {
                        snprintf(status_text, sizeof(status_text), "%s", "MAIN SLOT B IS EMPTY");
                        status_col = D_RED;
                        dirty = true;
                    } else {
                        return (RecoveryBootMenuChoice)selected;
                    }
                }
            }
        }

        sleep_ms(30);
    }
}
#endif

static void _snapshot_update(void) {
    uint32_t s = spin_lock_blocking(g_bat_lock);
    bat_snapshot(&g_bat, &g_bat_snapshot);
    spin_unlock(g_bat_lock, s);
}

static void _check_brownout(void) {
    bool now_brownout = g_bat.voltage < VBAT_BROWNOUT_V;
    log_set_brownout(now_brownout);

    if (!g_prev_brownout && now_brownout) {
        log_brownout_enter(&g_logger, g_bat.voltage);
        stats_inc_brownout(&g_stats);
    } else if (g_prev_brownout && !now_brownout) {
        log_brownout_exit(&g_logger, g_bat.voltage);
    }

    g_prev_brownout = now_brownout;
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
        log_i2c_recovery(&g_logger, g_bat.i2c_fail_count);
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

    if (!g_prev_charger_present && present) {
        log_charger_present(&g_logger, g_bat.voltage, g_bat.i_chg);
    } else if (g_prev_charger_present && !present) {
        log_charger_lost(&g_logger, g_bat.voltage);
    }
    g_prev_charger_present = present;
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
        if (!g_fan_blocked_logged) {
            log_fan_blocked(&g_logger, LOG_REASON_NO_SENSOR);
            g_fan_blocked_logged = true;
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
        log_fan_on(&g_logger, g_bat.temp_bat, g_bat.temp_inv);
        printf("[THERM] fan ON: bat=%.1fC dc-usb=%.1fC\n", g_bat.temp_bat, g_bat.temp_inv);
    } else if (g_fan_forced_on && below_off) {
        pwr_disable(&g_pwr, PORT_FAN);
        g_fan_forced_on = false;
        log_fan_off(&g_logger, g_bat.temp_bat, g_bat.temp_inv);
        printf("[THERM] fan OFF: bat=%.1fC dc-usb=%.1fC\n", g_bat.temp_bat, g_bat.temp_inv);
    }
}

// в"Ђв"Ђ Startup validation в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
#if !POWERSTATION_RECOVERY_SLOT
static const char *_diag_status(bool ok, bool warn_only) {
    if (ok) return "OK";
    return warn_only ? "WARN" : "FAIL";
}

static uint16_t _diag_color(bool ok, bool warn_only) {
    if (ok) return D_GREEN;
    return warn_only ? D_ORANGE : D_RED;
}
#endif

static void _bootdiag_draw_progress(const char *title, const char *line1, const char *line2) {
    disp_fill(&g_disp, D_BG);
    disp_header(&g_disp, title, FW_VERSION);
    disp_text_center(&g_disp, 72, "Power on self-test", D_ACCENT);
    disp_text_center(&g_disp, 108, line1, D_TEXT);
    if (line2 && line2[0]) disp_text_center(&g_disp, 126, line2, D_SUBTEXT);
    disp_text_center(&g_disp, 214, "Checking sensors...", D_SUBTEXT);
    disp_flush_sync(&g_disp);
}

#if !POWERSTATION_RECOVERY_SLOT
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
        snprintf(buf, sizeof(buf), "OK=run auto %lus", (unsigned long)(timeout_left_ms / 1000u));
    } else {
        snprintf(buf, sizeof(buf), "OK=diag auto %lus", (unsigned long)(timeout_left_ms / 1000u));
    }
    disp_footer(&g_disp, buf, NULL);
    disp_flush_sync(&g_disp);
}
#endif

#if !POWERSTATION_RECOVERY_SLOT
static bool _bootdiag_wait_confirm(const StartupDiagReport *rep, bool *force_diag_ui) {
    uint32_t confirm_ms = rep->startup_ok ? 3000u : BOOT_DIAG_CONFIRM_MS;
    uint32_t start_ms = to_ms_since_boot(get_absolute_time());
    bool ok_prev = false;

    while (1) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        uint32_t elapsed = now_ms - start_ms;
        uint32_t timeout_left = (elapsed >= confirm_ms) ? 0u : (confirm_ms - elapsed);
        _bootdiag_draw_report(rep, timeout_left);

        bool ok_now = (gpio_get(BTN_OK_PIN) == 0);
        if (ok_now && !ok_prev) {
            *force_diag_ui = !rep->startup_ok;
            return true;
        }
        if (elapsed >= confirm_ms) {
            *force_diag_ui = !rep->startup_ok;
            return true;
        }

        ok_prev = ok_now;
        sleep_ms(40);
    }
}
#endif

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
    esp_init(&g_esp, &g_logger, &g_stats, &g_pwr, _esp_store_settings);
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
    _boot_buttons_init();
    _early_console_init();
#if POWERSTATION_RECOVERY_SLOT
    g_recovery_boot_menu_requested = _consume_recovery_boot_menu_request();
    g_boot_ota_safe_requested = !g_recovery_boot_menu_requested;
    printf("[BOOT] recovery slot build -> %s\n",
           g_recovery_boot_menu_requested ? "boot menu requested" : "OTA recovery forced");
#else
    g_boot_ota_safe_requested = false;
    printf("[BOOT] main slot build -> normal loader-controlled boot\n");
#endif
#if DEBUG_USB_BRINGUP
    printf("[SEQ] DEBUG_USB_BRINGUP=1: pseq_latch skipped, relays forced OFF\n");
#else
    printf("[SEQ] SYSTEM_HOLD asserted on GP%u after %ums bootstrap delay\n",
           GPIO_PWR_LATCH, PWR_BOOTSTRAP_DELAY_MS);
#endif

    // в"Ђв"Ђ 2. INIT в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
    _init_all();

#if POWERSTATION_RECOVERY_SLOT
    if (g_recovery_boot_menu_requested) {
        RecoveryBootMenuChoice boot_choice = _run_recovery_boot_menu();
        if (boot_choice == RECOVERY_BOOT_MENU_MAIN) {
            printf("[BOOT] recovery menu -> reboot to main slot B\n");
            _reboot_to_requested_slot(BOOT_SLOT_B);
        }
        if (boot_choice == RECOVERY_BOOT_MENU_USB) {
            printf("[BOOT] recovery menu -> BOOTSEL\n");
            reset_usb_boot(0u, 0u);
        }
        g_boot_ota_safe_requested = true;
        printf("[BOOT] recovery menu -> Pico OTA recovery\n");
    }
#endif

    // в"Ђв"Ђ 3. VALIDATION в"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђв"Ђ
    StartupDiagReport diag = {0};
    bool startup_ok = _startup_validate(&diag);
    bool force_diag_ui = false;
#if !POWERSTATION_RECOVERY_SLOT
    _bootdiag_wait_confirm(&diag, &force_diag_ui);
#endif
    float soc_ocv   = diag.soc_ocv;
    /* Re-seed EKF with validated voltage SOC (bat_init used nominal) */
    {
        float soc_frac = soc_ocv / 100.0f;
        ekf_init(&g_bat.ekf, soc_frac);
        rint_update_soc(&g_bat.ekf.rint, soc_frac, g_bat.temp_bat);
        g_bat.ekf.r0 = rint_r0(&g_bat.ekf.rint, g_bat.temp_bat);
        g_bat.ekf.r1 = rint_r1(&g_bat.ekf.rint, g_bat.temp_bat);
        g_bat.ekf.c1 = rint_c1(&g_bat.ekf.rint);
        g_bat.soc = soc_ocv;
        printf("[BOOT] EKF re-seeded: SOC=%.1f%% V=%.3fV\n",
               soc_ocv, g_bat.voltage);
    }
    BootMode mode   = pseq_resolve(&g_pseq, startup_ok, soc_ocv,
                                   g_boot_ota_safe_requested);
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
        log_safe_mode_enter(&g_logger, LOG_REASON_SAFE_MODE, soc_ocv, g_bat.voltage);
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
        log_safe_mode_enter(&g_logger, LOG_REASON_AUTO, soc_ocv, g_bat.voltage);
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
        /* Use validated OCV-based SOC, not stale bat_init() value */
        log_boot(&g_logger, soc_ocv, g_bat.voltage);
    } else {
        printf("[BOOT] core1 not ready, boot log skipped\n");
    }
    _maybe_confirm_running_slot();
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

            /* Sensor fault/recovered tracking */
            {
                uint8_t cur_valid = g_bat.meas_valid;
                uint8_t lost     = g_prev_meas_valid & ~cur_valid;
                uint8_t restored = ~g_prev_meas_valid & cur_valid;
                static const uint8_t sensor_bits[] = {
                    MEAS_VALID_PACK, MEAS_VALID_CHG, MEAS_VALID_CELLS,
                    MEAS_VALID_TBAT, MEAS_VALID_TINV
                };
                for (int si = 0; si < 5; si++) {
                    if (lost & sensor_bits[si]) {
                        log_sensor_fault(&g_logger, si, (uint32_t)cur_valid);
                        stats_inc_sensor_fault(&g_stats);
                    }
                    if (restored & sensor_bits[si])
                        log_sensor_recovered(&g_logger, si, (uint32_t)cur_valid);
                }
                g_prev_meas_valid = cur_valid;
            }

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

            uint32_t new_alarms;
            {
                uint32_t current_alarms = g_prot.alarms;
                uint32_t cleared_alarms;
                new_alarms = current_alarms & ~prev_alarms;
                cleared_alarms = prev_alarms & ~current_alarms;

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
                    log_alarm_enter(&g_logger, new_alarms,
                                    snap.display_soc_pct, snap.voltage, snap.temp_bat);
                }

                if (cleared_alarms) {
                    log_alarm_exit(&g_logger, cleared_alarms,
                                   snap.display_soc_pct, snap.voltage, snap.temp_bat);
                }

                prev_alarms = current_alarms;
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

        // Remote shutdown via ESP bridge
        if (g_esp.shutdown_requested) {
            g_esp.shutdown_requested = false;
            printf("[MAIN] remote shutdown requested via ESP\n");
            pseq_user_poweroff(&g_pseq, &g_buz);
        }

        buz_tick(&g_buz);
    }

    return 0;
}
