# ESP32-S2 Bridge Firmware

This subproject is the ESP32-S2 side of the PowerStation integration.

## Target Module

- WEMOS LOLIN ESP32-S2 Mini
- ESP32-S2FN4R2
- 4 MB flash
- 2 MB PSRAM
- onboard LED on `GPIO15`

## What It Does

- Connects to the RP2040 PowerStation board over UART
- Caches live telemetry, stats, settings and event log slices
- Serves a local web UI
- Exposes REST/JSON endpoints
- Exports stats and logs as JSON or CSV
- Supports OTA in two ways:
  - web upload through the browser
  - `ArduinoOTA` when PowerStation is switched to `OTA` mode

## Default Hardware Mapping

- ESP `RX` <= RP2040 `GP20`
- ESP `TX` => RP2040 `GP21`
- ESP `EN` or power switch <= RP2040 `GP22`

Default ESP UART pins in `platformio.ini`:

- `PSTATION_UART_RX_PIN=16`
- `PSTATION_UART_TX_PIN=17`

Change them there if your ESP32-S2 board uses different pins.

## Build

This project targets PlatformIO with Arduino framework:

```bash
cd esp32_s2_bridge
pio run
```

It also compiles with Arduino CLI using the installed Arduino IDE toolchain:

```bash
"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --fqbn esp32:esp32:lolin_s2_mini C:\pico\pico_powerstation\esp32_s2_bridge
```

Verified locally on 2026-04-03.

## Network Modes

- `AP`
- `STA`
- `AP+STA`

The web UI can update Wi-Fi settings and stores them in ESP preferences.

## RP2040 Protocol

The bridge consumes JSON lines from RP2040:

- `hello`
- `telemetry`
- `stats`
- `settings`
- `log_meta`
- `log_event`
- `ack`
- `error`

It sends commands/status back:

- `HELLO`
- `PING`
- `GET STATS`
- `GET SETTINGS`
- `GET LOG <start> <count>`
- `SET <key> <value>`
- `ACTION LOG_RESET`
- `ACTION STATS_RESET`
- `STATUS <text>`

## Current Web/OTA Scope

- AP, STA or AP+STA profile stored in ESP `Preferences`
- browser UI for telemetry, stats, settings and export
- JSON endpoints for telemetry, stats, cache, RP2040 settings and network settings
- CSV/JSON export for stats and event log
- browser OTA upload endpoint
- `ArduinoOTA` service for OTA mode with status reflected back to PowerStation UI

## Detailed Guide

See the detailed feature overview and Arduino/USB flashing guide here:

- `docs/ESP32_S2_WEB_UI_OTA_GUIDE.md`
