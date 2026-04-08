# Pico PowerStation OTA ESP: code description

## Purpose

`Pico PowerStation OTA ESP` is a firmware stack for an RP2040 + ESP32-S2 based system where:

- `RP2040 / Pico` handles power control, BMS logic, protection, UI, and persistent storage.
- `ESP32-S2 bridge` provides Wi-Fi, a web UI, OTA for the ESP itself, and a dedicated OTA path for the Pico.

The current architecture is built around:

- `loader`
- `slot A = recovery`
- `slot B = main`

That means the normal runtime firmware lives in `slot B`, while `slot A` acts as a recovery environment for the boot menu and Pico OTA updates.

## Overall architecture

### Pico / RP2040

The main build targets are defined in [CMakeLists.txt](../CMakeLists.txt):

- `pico_powerstation_loader` — standalone boot loader
- `pico_powerstation_slot_a` — recovery build
- `pico_powerstation_slot_b` — main runtime build
- `combined_loader_recovery_main.uf2` — full Pico UF2 image for initial flashing

Flash layout:

- `loader` lives at the beginning of flash
- `slot A` is the recovery image region
- `slot B` is the main firmware region

The loader is implemented in [src/main_ota_loader.c](../src/main_ota_loader.c). It:

- loads the boot state from `boot_control`
- validates the available slots
- supports forced boot into a specific slot via watchdog scratch registers
- starts recovery `slot A` when the `Down` button is held during power-on
- passes a recovery-menu request flag into the recovery firmware

### Boot menu

The recovery boot menu is implemented in [src/main.c](../src/main.c).

When `Down` is held during power-on, the user is no longer dropped directly into OTA. Instead, recovery opens a boot menu with three options:

- `PICO OTA`
- `BOOT MAIN`
- `USB BOOTSEL`

The behavior is:

- `PICO OTA` keeps the device in recovery and waits for OTA from the ESP
- `BOOT MAIN` reboots through the loader into `slot B`
- `USB BOOTSEL` enters the standard RP2040 USB boot mode for UF2 flashing

### Main Pico firmware

The main application logic is organized under:

- [src/app](../src/app)
- [src/bms](../src/bms)
- [src/drivers](../src/drivers)

Major functional blocks:

- power control and startup sequencing
- BMS logic, SOC/SOH estimation, statistics, and persistent data
- current, voltage, and temperature sensor integration
- ST7789 user interface
- logging, settings, and flash storage
- protection logic, limits, and system state handling

Key high-level modules:

- [src/app/boot_control.c](../src/app/boot_control.c) — boot state, slot selection, rollback, and boot-stage handoff
- [src/app/ota_manager.c](../src/app/ota_manager.c) — Pico OTA receive path and `slot B` image writing
- [src/app/esp_manager.c](../src/app/esp_manager.c) — UART protocol between Pico and ESP
- [src/app/ui.c](../src/app/ui.c) — UI, recovery/OTA screens, and system views
- [src/app/power_sequencer.c](../src/app/power_sequencer.c) — startup sequencing and power modes

## ESP32-S2 bridge

The ESP side lives in [esp32_s2_bridge](../esp32_s2_bridge).

It has three roles:

- web bridge for the main dashboard and settings
- OTA target for the ESP itself
- dedicated Pico OTA bridge for transferring `.bin` images to Pico recovery

The main logic is implemented in [esp32_s2_bridge/src/main.cpp](../esp32_s2_bridge/src/main.cpp).

Key characteristics:

- a dedicated `PICO OTA` bridge mode
- a separate Pico OTA page with no access to the general dashboard during recovery sessions
- Pico firmware staging in `SPIFFS`
- asynchronous chunk-based UART transfer to the Pico
- Wi-Fi sleep disabled for a more stable OTA transport

The UI is split into:

- [esp32_s2_bridge/src/web_ui.h](../esp32_s2_bridge/src/web_ui.h) — general dashboard
- [esp32_s2_bridge/src/pico_ota_ui.h](../esp32_s2_bridge/src/pico_ota_ui.h) — dedicated Pico OTA page

## Pico OTA mechanism

The current Pico update flow is:

1. The user holds `Down` while powering on the Pico.
2. The loader enters recovery `slot A` and opens the boot menu.
3. `PICO OTA` is selected in the boot menu.
4. The ESP detects that Pico recovery OTA mode is active and serves the dedicated `/pico-ota` page.
5. The `pico_powerstation_main_slot_b.bin` file is uploaded to the ESP.
6. The ESP first stores the full image in staging (`SPIFFS`), then sends it to the Pico over UART in chunks.
7. Pico recovery receives the image and writes it into `slot B`.
8. After a successful finish, the device reboots and the loader switches execution to `slot B`.

## Current artifacts

The current release package is stored in [release/1.0](../release/1.0):

- [combined_loader_recovery_main.uf2](../release/1.0/combined_loader_recovery_main.uf2) — full Pico image
- [pico_powerstation_recovery_slot_a.bin](../release/1.0/pico_powerstation_recovery_slot_a.bin) — recovery slot image
- [pico_powerstation_main_slot_b.bin](../release/1.0/pico_powerstation_main_slot_b.bin) — main Pico firmware for OTA
- [esp32_s2_bridge_ota.bin](../release/1.0/esp32_s2_bridge_ota.bin) — current ESP bridge application image

## Release 1.0 status

Release `1.0` captures the following state:

- stabilized `loader + recovery slot A + main slot B` architecture
- boot menu when holding `Down` during power-on
- isolated Pico OTA mode on ESP with a dedicated web page
- refreshed Pico and ESP release artifacts
- verified end-to-end flow: flashing, reboot, and return to normal runtime
