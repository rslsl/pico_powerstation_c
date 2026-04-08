# PowerStation OTA ESP Build Guide

## Scope

This repository contains two firmware projects that work together:

- `RP2040 / Pico` firmware built with `CMake` + `Pico SDK`
- `ESP32-S2 bridge` firmware built with `PlatformIO` (Arduino framework)

The current production architecture is:

- `loader`
- `slot A = recovery`
- `slot B = main runtime`

Normal operation runs from `slot B`. Recovery and Pico OTA entry live in `slot A`. The ESP32-S2 provides Wi-Fi, web UI, OTA for itself, and UART-based OTA delivery for the Pico.

## Repository Layout

```text
Powerstation_OTA_2/
|-- CMakeLists.txt
|-- pico_sdk_import.cmake
|-- BUILD.md
|-- README.md
|-- workplan.md
|-- memmap_16mb.ld
|-- src/
|   |-- config.h
|   |-- main.c
|   |-- main_ota_loader.c
|   |-- main_tiny_slot_test.c
|   |-- main_periph_test_new_pinout.c
|   |-- main_cpp_display_test.cpp
|   |-- app/
|   |-- bms/
|   |-- drivers/
|   `-- third_party/
|-- esp32_s2_bridge/
|   |-- platformio.ini
|   |-- README.md
|   `-- src/
|-- tools/
|-- docs/
|-- OTA_ready/
|-- build/
`-- Unused/
```

## Active Working Folders

If you are navigating the project day to day, these are the folders that matter most:

- `src/` - active RP2040 firmware code
- `esp32_s2_bridge/` - active ESP32-S2 bridge firmware
- `OTA_ready/` - current deployment-ready artifacts
- `docs/` - reference documentation and flashing guides
- `docs/notes/` - auxiliary historical notes moved out of the repository root
- `tools/` - helper scripts for combined UF2 generation

Folders that are not part of the normal edit/build loop:

- `build/` - generated Pico build output
- `esp32_s2_bridge/.pio/` - generated ESP build output
- `Unused/` - archived old builds, backups, experiments

## Main Files To Work In

For the most common changes, these are the primary code files:

- `src/main.c` - main Pico runtime flow, startup, BMS loop, UI scheduling
- `src/main_ota_loader.c` - boot loader and slot handoff logic
- `src/config.h` - global pin map, flash layout, thresholds, timings
- `src/app/ui.c` - Pico UI, screens, status display, recovery flow
- `src/app/esp_manager.c` - Pico side of ESP UART protocol, logs, OTA control
- `src/app/ota_manager.c` - Pico OTA write path into slot B
- `src/bms/battery.c` - battery state integration and measurement pipeline
- `src/bms/bms_logger.c` - event log storage and timestamp handling
- `esp32_s2_bridge/src/main.cpp` - ESP web server, bridge cache, OTA, NTP
- `esp32_s2_bridge/src/web_ui.h` - main browser UI
- `esp32_s2_bridge/src/pico_ota_ui.h` - dedicated Pico OTA browser page

## Code Structure

### Pico firmware

Top-level Pico entry points:

- `src/main.c` - main runtime firmware used by the monolithic target and OTA slots
- `src/main_ota_loader.c` - standalone boot loader
- `src/main_tiny_slot_test.c` - minimal slot-A boot-chain validation target
- `src/main_periph_test_new_pinout.c` - peripheral bring-up / pinout test
- `src/main_cpp_display_test.cpp` - ST7789 display test target

Application layer in `src/app/`:

- `boot_control.*` - boot metadata, slot selection, confirmation, rollback limits
- `ota_manager.*` - Pico OTA receive/write path for `slot B`
- `esp_manager.*` - UART protocol with the ESP bridge, telemetry, settings, logs, OTA control
- `power_sequencer.*` - startup latch flow and boot mode resolution
- `power_control.*` - relay / output control
- `protection.*` - alarms, cutoffs, safety actions
- `save_manager.*` - deferred persistence to flash
- `session_manager.*` - charge/discharge session accounting
- `system_settings.*` - persistent settings storage and application
- `display.*`, `ui.*`, `ui_assets.*` - ST7789 framebuffer UI and recovery/runtime screens
- `buzzer.*` - non-blocking sound patterns

BMS layer in `src/bms/`:

- `battery.*` - central battery state and sensor integration
- `bms_ekf.*` - SOC estimator
- `bms_ocv.*` - OCV tables / voltage model
- `bms_rint.*` - internal resistance model
- `bms_soh.*` - SOH tracking
- `bms_predictor.*` - time-left prediction
- `bms_logger.*` - flash-backed event log ring buffer
- `bms_stats.*` - aggregate counters and history stats
- `flash_nvm.*` - flash read/write helpers

Hardware drivers in `src/drivers/`:

- `tca9548a.*` - I2C multiplexer
- `ina226.*` - charge/discharge path monitors
- `ina3221.*` - cell rail monitor
- `lm75a.*` - battery / inverter temperature sensors
- `st7789.*` - display wrapper around bundled third-party driver

Bundled third-party code:

- `src/third_party/ST7789_TFT_PICO/`

### ESP32-S2 bridge

ESP bridge sources live in `esp32_s2_bridge/src/`:

- `main.cpp` - UART bridge, REST API, OTA logic, NTP sync, SPIFFS staging, cache
- `web_ui.h` - main dashboard HTML/CSS/JS compiled into firmware
- `pico_ota_ui.h` - dedicated Pico OTA page

Key bridge responsibilities:

- cache Pico `hello`, `telemetry`, `stats`, `settings`, `ota`, and event-log slices
- provide web UI and JSON endpoints
- export logs and stats as JSON / CSV
- stage Pico `.bin` images in SPIFFS and deliver them over UART
- run OTA for the ESP itself
- send NTP-derived epoch time to the Pico when available

## Build Requirements

### Pico / RP2040

- `Pico SDK >= 2.0.0`
- `CMake >= 3.13`
- `arm-none-eabi-gcc` toolchain
- `Python 3`
- a generator such as `Ninja` or `Unix Makefiles`

Typical packages on Debian/Ubuntu:

```bash
sudo apt install cmake ninja-build gcc-arm-none-eabi libnewlib-arm-none-eabi \
                 build-essential libstdc++-arm-none-eabi-newlib python3
```

SDK setup example:

```bash
git clone https://github.com/raspberrypi/pico-sdk.git --branch 2.0.0
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=$(pwd)
```

### ESP32-S2

- `Python 3`
- `PlatformIO`
- ESP32 Arduino toolchain packages installed by PlatformIO

If `pio` is not on `PATH`, the project also builds with:

```bash
python -m platformio run
```

Current board target from `esp32_s2_bridge/platformio.ini`:

- `lolin_s2_mini`
- UART bridge pins:
  - `PSTATION_UART_RX_PIN = 16`
  - `PSTATION_UART_TX_PIN = 17`
  - `PSTATION_UART_BAUD = 115200`

## Pico Build Targets

Defined in `CMakeLists.txt`:

- `pico_powerstation` - monolithic full application build
- `pico_powerstation_loader` - OTA boot loader
- `pico_powerstation_slot_a` - recovery image
- `pico_powerstation_slot_b` - main runtime image
- `pico_tiny_slot_test_a` - minimal slot-A validation image
- `periph_test_new_pinout` - hardware pinout/peripheral test
- `main_cpp_display_test` - display test

Combined UF2 targets are generated when Python is available:

- `combined_loader_tiny_a`
- `combined_loader_slot_a`
- `combined_loader_recovery_main`

## CMake Options

Supported options in `CMakeLists.txt`:

- `POWERSTATION_FLASH_16MB=ON|OFF`
  - default: `OFF`
  - `OFF` builds for `2 MB` flash
  - `ON` builds for `16 MB` flash and switches linker script to `memmap_16mb.ld`
- `POWERSTATION_USB_BRINGUP=ON|OFF`
  - default: `OFF`
  - only meaningful in non-production builds
  - keeps startup in a safer USB-debug-oriented mode
- `POWERSTATION_OTA_LOADER_DEBUG=ON|OFF`
  - default: `OFF`
  - enables USB stdio in the loader

## Pico Build Commands

Configure a fresh release build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

If you prefer default generator selection:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Debug build:

```bash
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --config Debug
```

16 MB build example:

```bash
cmake -S . -B build-16m -G Ninja -DCMAKE_BUILD_TYPE=Release -DPOWERSTATION_FLASH_16MB=ON
cmake --build build-16m --config Release
```

USB bring-up example:

```bash
cmake -S . -B build-usb -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPOWERSTATION_USB_BRINGUP=ON
cmake --build build-usb --config Debug
```

## Pico Output Artifacts

Typical outputs in `build/`:

- `pico_powerstation.uf2`
- `pico_powerstation_loader.uf2`
- `pico_powerstation_slot_a.uf2`
- `pico_powerstation_slot_b.uf2`
- `pico_powerstation_loader.bin`
- `pico_powerstation_slot_a.bin`
- `pico_powerstation_slot_b.bin`
- `combined_loader_slot_a.uf2`
- `combined_loader_recovery_main.uf2`

Current deployment-ready artifacts are stored in `OTA_ready/`:

- `OTA_ready/combined_loader_recovery_main.uf2`
- `OTA_ready/pico_powerstation_slot_b.bin`
- `OTA_ready/esp32_s2_bridge.bin`

## ESP32-S2 Build Commands

Build the bridge firmware:

```bash
cd esp32_s2_bridge
python -m platformio run
```

Upload directly to a connected ESP32-S2:

```bash
cd esp32_s2_bridge
python -m platformio run -t upload --upload-port COM16
```

Resulting firmware image:

- `esp32_s2_bridge/.pio/build/esp32-s2-bridge/firmware.bin`

This image is copied into:

- `OTA_ready/esp32_s2_bridge.bin`

Notes:

- the web UI is compiled into headers; there is no separate `uploadfs` step for dashboard assets
- SPIFFS is still used at runtime for staging Pico OTA images

## Flash Layout

Persistent tail layout is derived from `FLASH_TOTAL` in `src/config.h` and `CMakeLists.txt`.

Current logic reserves:

- loader region: `128 KB`
- application region: from `128 KB` offset up to the persistent flash tail
- slot A and slot B: equal halves of that application region
- persistent tail near flash end:
  - event log
  - relay state
  - settings
  - stats/history
  - SOH

Important constants:

- `PICO_OTA_LOADER_SIZE = 128 KB`
- `FLASH_LOG_OFFSET = FLASH_TOTAL - 72 * 4096`
- `PICO_OTA_SLOT_SIZE = (FLASH_LOG_OFFSET - PICO_OTA_APP_REGION_OFFSET) / 2`

For the default `2 MB` build this produces:

- loader at flash base
- recovery `slot A`
- main `slot B`
- persistent NVM tail before flash end

## Current OTA / Flashing Flows

### Full Pico recovery or first flash

Use:

- `OTA_ready/combined_loader_recovery_main.uf2`

Steps:

1. Hold `BOOTSEL` on the Pico.
2. Connect USB.
3. Copy `combined_loader_recovery_main.uf2` to `RPI-RP2`.

This flashes:

- loader
- recovery `slot A`
- main `slot B`

### Pico OTA through ESP

Use:

- `OTA_ready/pico_powerstation_slot_b.bin`

Flow:

1. Hold `Down` during power-on to enter the recovery boot menu.
2. Select `PICO OTA`.
3. Open the ESP bridge OTA page.
4. Upload `pico_powerstation_slot_b.bin`.

This updates only `slot B`.

### ESP bridge update

Use one of:

- direct serial upload from `esp32_s2_bridge`
- `OTA_ready/esp32_s2_bridge.bin` if your existing ESP bootloader/partitions already match the current PlatformIO project

## Runtime Communication Notes

Pico and ESP exchange line-based UART messages.

Pico -> ESP JSON messages include:

- `hello`
- `telemetry`
- `stats`
- `settings`
- `ports`
- `ota`
- `log_meta`
- `log_event`
- `ack`
- `error`

ESP -> Pico commands include:

- `HELLO`
- `PING`
- `GET TELEMETRY`
- `GET STATS`
- `GET SETTINGS`
- `GET PORTS`
- `GET LOG <start> <count>`
- `GET OTA`
- `SET <key> <value>`
- `ACTION LOG_RESET`
- `ACTION STATS_RESET`
- `ACTION PORT_SET <port> <on|off|toggle>`
- `ACTION SHUTDOWN`
- `OTA BEGIN <size> <version>`
- `OTA CHUNK <offset> <hex>`
- `OTA END <crc32>`
- `OTA ABORT`

Current log transport behavior:

- Pico prepares log events first, skips unreadable entries, and reports `sent` as the actual number of transmitted rows
- ESP requests logs in batches of `8` rows by default

## Build Notes And Caveats

- `pico_powerstation` still exists and builds as a monolithic image, but the operational OTA architecture is `loader + slot A + slot B`
- `OTA_ready/` is the current working handoff folder for deployable files
- `Unused/` contains archived old builds, experiments, and backups; it is not part of the active build flow
- `release/1.0` style paths referenced by older docs are not the current primary artifact location in this repository

## Recommended Verification After Build

After rebuilding the current project, verify at least:

1. `build/combined_loader_recovery_main.uf2` is regenerated for full Pico recovery.
2. `build/pico_powerstation_slot_b.bin` exists for Pico OTA.
3. `esp32_s2_bridge/.pio/build/esp32-s2-bridge/firmware.bin` exists for ESP.
4. `OTA_ready/` contains the files intended for deployment.
5. Event Log loads successfully from the ESP web UI after flashing both sides.
