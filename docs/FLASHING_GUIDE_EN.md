# PowerStation OTA ESP: Flashing Guide

## Current Ready-To-Use Files

The current flashing files are stored in `../OTA_ready/`:

- `combined_loader_recovery_main.uf2`
- `pico_powerstation_slot_b.bin`
- `esp32_s2_bridge.bin`

These are the active artifacts that match the current repository layout.

## 1. Full Pico Flash / Recovery

Use this method for:

- first flash
- full device recovery
- restoring loader + recovery + main in one pass

File to use:

- `../OTA_ready/combined_loader_recovery_main.uf2`

Steps:

1. Power the Pico off.
2. Hold `BOOTSEL`.
3. Connect the Pico to USB.
4. Wait for the `RPI-RP2` drive.
5. Copy `combined_loader_recovery_main.uf2` to that drive.
6. Wait for the copy to finish and for the board to reboot.

Result:

- `loader` is flashed
- recovery `slot A` is flashed
- main `slot B` is flashed

## 2. Flashing The ESP32-S2 Bridge

### Option A: standard serial upload from the current PlatformIO project

This is the recommended method when the ESP32-S2 board is connected to the PC.

Build and upload:

```powershell
cd C:\pico\Powerstation_OTA_2\esp32_s2_bridge
python -m platformio run -t upload --upload-port COM16
```

Adjust `COM16` to the actual ESP serial port.

### Option B: use the current ready-made ESP image

Use:

- `../OTA_ready/esp32_s2_bridge.bin`

This is appropriate when:

- the device already runs a compatible ESP bootloader / partition layout
- you are updating through the existing OTA flow or another known-good write path

## 3. Entering The Pico Boot Menu

`Down` no longer jumps directly into OTA. It opens the recovery boot menu.

Steps:

1. Fully power the system off.
2. Hold `Down`.
3. Power the system on while still holding the button.
4. Wait for the boot menu.

The menu contains:

- `PICO OTA`
- `BOOT MAIN`
- `USB BOOTSEL`

## 4. Updating Pico Through ESP OTA

This flow updates only `slot B`.

File to use:

- `../OTA_ready/pico_powerstation_slot_b.bin`

Steps:

1. Enter the Pico boot menu.
2. Select `PICO OTA`.
3. Connect to the ESP bridge over Wi-Fi or the local network.
4. Open the dedicated Pico OTA page served by the ESP.
5. Upload `pico_powerstation_slot_b.bin`.
6. Wait for ESP staging and UART transfer to complete.
7. After completion, Pico should reboot into the normal runtime.

## 5. Returning To Normal Mode Without OTA

If you only want to leave recovery:

1. Open the boot menu with `Down` during startup.
2. Select `BOOT MAIN`.

## 6. Returning To USB BOOTSEL

If OTA is unavailable or a full recovery is needed:

1. Open the boot menu with `Down` during startup.
2. Select `USB BOOTSEL`.
3. Flash `../OTA_ready/combined_loader_recovery_main.uf2`.

## 7. Common Service Scenarios

- Full Pico recovery: `combined_loader_recovery_main.uf2`
- Pico OTA through ESP: `pico_powerstation_slot_b.bin`
- ESP bridge update: `esp32_s2_bridge.bin` or serial upload from `esp32_s2_bridge`

## 8. Notes About Older Files

Older release bundles and archived flashing files are no longer the primary source of truth.

Use `OTA_ready/` as the current active folder for deployment files.

If you need a separate recovery `slot A` image or other non-default artifacts, rebuild them from the current `CMake` project rather than relying on older release folders.
