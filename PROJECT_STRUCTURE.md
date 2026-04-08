# Project Structure

Quick map of the active code and file layout in `Powerstation_OTA_2`.

## Start Here

If you need to understand the project fast, open these first:

- `README.md` - repository overview
- `BUILD.md` - build flow, targets, artifacts, OTA layout
- `workplan.md` - current project context and recent changes
- `src/config.h` - pins, timings, flash layout, thresholds

## Active Code Roots

- `src/` - Pico / RP2040 firmware
- `esp32_s2_bridge/` - ESP32-S2 bridge firmware

## Pico Firmware Map

Top-level entry points:

- `src/main.c` - main runtime
- `src/main_ota_loader.c` - boot loader
- `src/main_tiny_slot_test.c` - tiny slot-A validation image
- `src/main_periph_test_new_pinout.c` - peripheral pinout test
- `src/main_cpp_display_test.cpp` - ST7789 display test

Main subfolders:

- `src/app/` - application logic and UI
- `src/bms/` - battery algorithms and persistence
- `src/drivers/` - hardware drivers
- `src/third_party/` - bundled external display library

Most important Pico files:

- `src/config.h` - central configuration
- `src/app/ui.c` - local UI and screensaver
- `src/app/esp_manager.c` - Pico <-> ESP protocol
- `src/app/ota_manager.c` - OTA write/verify logic
- `src/app/boot_control.c` - slot state and confirmation
- `src/bms/battery.c` - battery state and sensor pipeline
- `src/bms/bms_logger.c` - event log storage
- `src/bms/bms_stats.c` - statistics and history

## ESP32-S2 Bridge Map

Main files:

- `esp32_s2_bridge/src/main.cpp` - UART bridge, REST API, OTA, NTP, cache
- `esp32_s2_bridge/src/web_ui.h` - main web dashboard
- `esp32_s2_bridge/src/pico_ota_ui.h` - dedicated Pico OTA page
- `esp32_s2_bridge/platformio.ini` - board target and build settings

Generated ESP output:

- `esp32_s2_bridge/.pio/`

## Documentation Map

- `README.md` - overview
- `BUILD.md` - authoritative build guide
- `PROJECT_STRUCTURE.md` - this quick map
- `docs/CODE_DESCRIPTION_EN.md` - architecture reference
- `docs/CODE_DESCRIPTION_UK.md` - architecture reference in Ukrainian
- `docs/FLASHING_GUIDE_EN.md` - flashing guide
- `docs/FLASHING_GUIDE_UK.md` - flashing guide
- `docs/notes/` - historical notes moved out of the root

## Build And Deployment Folders

- `build/` - Pico CMake build output
- `OTA_ready/` - current firmware files to use for flashing and OTA
- `tools/` - helper scripts for combined UF2 creation

Current important files in `OTA_ready/`:

- `combined_loader_recovery_main.uf2`
- `pico_powerstation_slot_b.bin`
- `esp32_s2_bridge.bin`

## Archived / Non-Active Folders

- `Unused/` - archived builds, old release bundles, experiments, backups
- `ui_assets/` - design and asset support materials, not part of the main firmware build loop

## Typical Task -> File

- Change pins, thresholds, flash layout -> `src/config.h`
- Change Pico screens or indicators -> `src/app/ui.c`
- Change Pico OTA behavior -> `src/app/ota_manager.c`
- Change Pico/ESP UART protocol -> `src/app/esp_manager.c`, `esp32_s2_bridge/src/main.cpp`
- Change browser dashboard -> `esp32_s2_bridge/src/web_ui.h`
- Change event log format or timing -> `src/bms/bms_logger.c`, `src/app/esp_manager.c`, `esp32_s2_bridge/src/main.cpp`
- Change boot flow / slot confirmation -> `src/main_ota_loader.c`, `src/app/boot_control.c`
