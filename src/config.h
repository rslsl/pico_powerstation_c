#pragma once
// ============================================================
// config.h - PowerStation 3S2P / RP2040 / 16MB flash build
// ============================================================

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
#define CFG_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define CFG_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

// Battery
#define BAT_CAPACITY_AH     34.0f
#define BAT_CAPACITY_AS     (BAT_CAPACITY_AH * 3600.0f)
#define BAT_CELLS           3
#define BAT_CELL_MAX_V      4.20f
#define BAT_CELL_OVP_V      4.25f
#define BAT_CELL_MIN_V      3.30f
#define BAT_FULL_V          12.60f
#define BAT_EMPTY_V         9.90f
#define BAT_NOMINAL_V       11.10f

// I2C0
#define I2C_PORT            i2c0
#define I2C_SDA_PIN         0
#define I2C_SCL_PIN         1
#define I2C_FREQ_HZ         400000
#define I2C_TIMEOUT_US      50000

// TCA9548A channels
#define TCA_ADDR            0x70
#define TCA_CH_DIS          0
#define TCA_CH_CHG          1
#define TCA_CH_INA3221      2
#define TCA_CH_LM75A_BAT    3
#define TCA_CH_LM75A_INV    4

// Sensor I2C addresses
#define INA226_DIS_ADDR     0x40
#define INA226_CHG_ADDR     0x40
#define INA3221_ADDR        0x43
#define LM75A_BAT_ADDR      0x48
#define LM75A_INV_ADDR      0x48

// Shunts and INA226 scaling
#define SHUNT_DIS_MOHM      0.75f
#define SHUNT_CHG_MOHM      0.75f
#define IMAX_DIS_A          100.0f
#define IMAX_CHG_A          50.0f

// SPI0 / ST7789
#define SPI_PORT            spi0
#define SPI_SCK_PIN         18
#define SPI_MOSI_PIN        19
#define SPI_CS_PIN          2
#define LCD_DC_PIN          3
#define LCD_RST_PIN         17
#define LCD_BL_PIN          255
#define SPI_BAUD_HZ         8000000
#define LCD_W               240
#define LCD_H               280
#define LCD_X_OFFSET        0
#define LCD_Y_OFFSET        20

// UI buttons
#define BTN_UP_PIN          9
#define BTN_OK_PIN          10
#define BTN_DOWN_PIN        11
#define BTN_DEBOUNCE_MS     30
#define BTN_LONG_MS         600
#define BTN_POWEROFF_MS     3000

// Power sequencing
#define EARLY_CONSOLE_SETTLE_MS 100
#define PWR_HOLD_ASSERT_MS   30
#define PWR_LATCH_SETTLE_MS  350
#define DISPLAY_INIT_SETTLE_MS 120
#define PWR_HOLD_RELEASE_MS  30
#define PWR_FAIL_DISPLAY_MS  4000
#define BOOT_DIAG_CONFIRM_MS 8000
#define VBAT_MIN_BOOT_V      9.0f

// Relays, active low
#define GPIO_DC_OUT         4
#define GPIO_USB_PD         5
#define GPIO_CHARGE_IN      6
#define GPIO_PWR_LATCH      7
#define GPIO_FAN            8
#define RELAY_OFF           1
#define RELAY_ON            0
#define MOSFET_OFF          RELAY_OFF
#define MOSFET_ON           RELAY_ON

// Fan relay control
#define FAN_ON_TEMP_C       45.0f
#define FAN_OFF_TEMP_C      40.0f

// Buzzer
#define BUZZER_PIN          12
#define BUZZER_ON           1
#define BUZZER_OFF          0

// Protection thresholds
#define SOC_WARN_PCT        20
#define SOC_CUTOFF_PCT      5
#define CELL_WARN_V         3.5f
#define CELL_CUT_V          3.3f
#define VBAT_WARN_V         10.8f
#define VBAT_CUT_V          9.9f
#define IDIS_WARN_A         45.0f
#define IDIS_CUT_A          55.0f
#define DELTA_WARN_MV       80.0f
#define DELTA_CUT_MV        150.0f
#define TEMP_BAT_WARN_C     40.0f
#define TEMP_BAT_BUZZ_C     50.0f
#define TEMP_BAT_SAFE_C     60.0f
#define TEMP_BAT_CUT_C      70.0f
#define TEMP_BAT_CHARGE_MIN_C 0.0f
#define TEMP_INV_WARN_C     50.0f
#define TEMP_INV_SAFE_C     75.0f
#define TEMP_INV_CUT_C      80.0f
#define I2C_MAX_FAILS       10

// Sensor sanity filters
#define SENSOR_SANITY_V_MIN       0.0f
#define SENSOR_SANITY_V_MAX       16.0f
#define SENSOR_SANITY_CELL_V_MAX  5.0f
#define SENSOR_SANITY_I_MAX_A     200.0f
#define SENSOR_SANITY_TEMP_MIN_C -40.0f
#define SENSOR_SANITY_TEMP_MAX_C  125.0f

// Timings
#define SENSOR_MS           200
#define LOGIC_MS            100
#define SAVE_MS             3600000
#define LOG_FLUSH_MS        3600000
#define SAVE_SOC_DELTA_PCT  5.0f
#define SAVE_SOH_DELTA      0.005f
#define VBAT_BROWNOUT_V     9.5f
#define OCV_SETTLE_MS       30000
#define OCV_IDLE_A          0.5f
#define DISPLAY_MS          100

// Flash NVM
#ifndef FLASH_TOTAL
#define FLASH_TOTAL           (2 * 1024 * 1024)
#endif
#define FLASH_SECTOR_SIZE     4096
#define FLASH_SOH_OFFSET      (FLASH_TOTAL - 1 * FLASH_SECTOR_SIZE)
#define FLASH_SOH_OFFSET_B    (FLASH_TOTAL - 2 * FLASH_SECTOR_SIZE)
#define FLASH_HIST_OFFSET     (FLASH_TOTAL - 3 * FLASH_SECTOR_SIZE)
#define FLASH_HIST_OFFSET_B   (FLASH_TOTAL - 4 * FLASH_SECTOR_SIZE)
#define FLASH_SETTINGS_OFFSET (FLASH_TOTAL - 5 * FLASH_SECTOR_SIZE)
#define FLASH_LOG_OFFSET      (FLASH_TOTAL - 69 * FLASH_SECTOR_SIZE)
#define FLASH_LOG_SECTORS     64
#define FLASH_LOG_SIZE        (FLASH_LOG_SECTORS * FLASH_SECTOR_SIZE)

CFG_STATIC_ASSERT((FLASH_TOTAL == (2 * 1024 * 1024)) || (FLASH_TOTAL == (16 * 1024 * 1024)),
                  "Flash layout must match 2MB or 16MB hardware");
CFG_STATIC_ASSERT((FLASH_SOH_OFFSET % FLASH_SECTOR_SIZE) == 0, "SOH slot A must be sector-aligned");
CFG_STATIC_ASSERT((FLASH_SOH_OFFSET_B % FLASH_SECTOR_SIZE) == 0, "SOH slot B must be sector-aligned");
CFG_STATIC_ASSERT((FLASH_HIST_OFFSET % FLASH_SECTOR_SIZE) == 0, "Stats slot A must be sector-aligned");
CFG_STATIC_ASSERT((FLASH_HIST_OFFSET_B % FLASH_SECTOR_SIZE) == 0, "Stats slot B must be sector-aligned");
CFG_STATIC_ASSERT((FLASH_SETTINGS_OFFSET % FLASH_SECTOR_SIZE) == 0, "Settings slot must be sector-aligned");
CFG_STATIC_ASSERT((FLASH_LOG_OFFSET % FLASH_SECTOR_SIZE) == 0, "Log region must be sector-aligned");
CFG_STATIC_ASSERT((FLASH_LOG_OFFSET + FLASH_LOG_SIZE) <= FLASH_TOTAL, "Log region must fit inside 16MB flash");

// ST7789 colors: canonical definitions are D_* in app/display.h.
// COL_* removed — they were unused and had different values from D_*,
// which would have caused silent color-mismatch bugs.

// UI animation / screensaver
#define UI_IDLE_SCREENSAVER_MS  15000
#define UI_ANIM_STEP_MS         100
#define UI_BLINK_STEP_MS        450
#define UI_REFRESH_MS           500
#define UI_BL_DIM_PCT           22
#define UI_BL_ACTIVE_PCT        80

// Firmware version
#ifndef FW_VERSION
#define FW_VERSION  "v4.0-C"
#endif

// USB bring-up mode:
// 1 = skip pseq_latch() and keep relay GPIO in safe OFF state so
// diagnostics over USB can run even if SYSTEM_HOLD wiring is not final.
// Set back to 0 for normal автономний запуск через latch.
#ifndef DEBUG_USB_BRINGUP
#define DEBUG_USB_BRINGUP   0
#endif
