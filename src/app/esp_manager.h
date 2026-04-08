#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/uart.h"
#include "ota_manager.h"
#include "power_control.h"
#include "system_settings.h"
#include "../bms/battery.h"
#include "../bms/bms_logger.h"
#include "../bms/bms_stats.h"

typedef bool (*EspStoreSettingsFn)(const SystemSettings *next, bool reconfigure_sensors);

typedef struct {
    BmsLogger *logger;
    BmsStats *stats;
    PowerControl *pwr;
    EspStoreSettingsFn store_settings;
    PicoOtaManager ota;

    bool uart_ready;
    bool powered;
    bool link_up;
    bool boot_ready;
    bool pico_ota_ready;
    bool preserve_power;
    uint8_t mode;

    uint32_t boot_release_ms;
    uint32_t ready_at_ms;
    uint32_t last_rx_ms;
    uint32_t last_hello_ms;
    uint32_t last_telemetry_ms;
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t cmd_errors;

    uint8_t rx_len;
    char rx_line[ESP_RX_LINE_MAX];
    char last_status[24];
    volatile bool shutdown_requested;
} EspManager;

void esp_init(EspManager *esp,
              BmsLogger *logger,
              BmsStats *stats,
              PowerControl *pwr,
              EspStoreSettingsFn store_settings);
void esp_set_boot_ready(EspManager *esp, bool ready);
void esp_set_pico_ota_ready(EspManager *esp, bool ready);
void esp_apply_settings(EspManager *esp, const SystemSettings *cfg);
void esp_update(EspManager *esp, const BatSnapshot *snap, uint32_t now_ms);

EspMode esp_mode_get(const EspManager *esp);
bool esp_is_powered(const EspManager *esp);
bool esp_is_link_up(const EspManager *esp);
const char *esp_mode_name(EspMode mode);
const char *esp_status_text(const EspManager *esp);
