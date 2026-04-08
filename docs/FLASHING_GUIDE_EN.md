# Pico PowerStation OTA ESP: flashing guide

## Ready-to-use files

The current release artifacts are stored in [release/1.0](../release/1.0):

- [combined_loader_recovery_main.uf2](../release/1.0/combined_loader_recovery_main.uf2)
- [pico_powerstation_recovery_slot_a.bin](../release/1.0/pico_powerstation_recovery_slot_a.bin)
- [pico_powerstation_main_slot_b.bin](../release/1.0/pico_powerstation_main_slot_b.bin)
- [esp32_s2_bridge_ota.bin](../release/1.0/esp32_s2_bridge_ota.bin)

## 1. Full Pico flashing via UF2

Use this method for first-time flashing or full recovery.

Steps:

1. Power the Pico off.
2. Press and hold `BOOTSEL`.
3. Connect the Pico to the PC over USB.
4. Wait for the `RPI-RP2` drive to appear.
5. Copy [combined_loader_recovery_main.uf2](../release/1.0/combined_loader_recovery_main.uf2) to that drive.
6. Wait for the copy to finish and for the board to reboot automatically.

Result:

- `loader` is flashed
- recovery `slot A` is flashed
- main `slot B` is flashed

## 2. Flashing the ESP32-S2 bridge

### Option A: standard upload from the prepared build

This is the recommended method for the working project.

Requirements:

- the ESP32-S2 is connected
- the `build/esp_upload` directory exists
- `arduino-cli` is available

Example command:

```powershell
"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" upload `
  -p COM16 `
  --fqbn esp32:esp32:lolin_s2_mini `
  --input-dir C:\pico\pico_powerstation_c\build\esp_upload `
  -v
```

This path writes the full set that matches the current bridge build.

### Option B: use the ready-made application image

[esp32_s2_bridge_ota.bin](../release/1.0/esp32_s2_bridge_ota.bin) is the current bridge application image.

Use it only in scenarios where the ESP bootloader and partition table already match the current project configuration.

## 3. Entering the Pico boot menu

`Down` at startup no longer jumps directly into OTA.

To open the boot menu:

1. Fully power the system off.
2. Hold `Down`.
3. Power the system on while still holding the button.
4. Wait for the boot menu to appear.

The boot menu contains:

- `PICO OTA`
- `BOOT MAIN`
- `USB BOOTSEL`

## 4. Updating Pico through ESP OTA

This flow updates only `slot B`.

Steps:

1. Enter the Pico boot menu.
2. Select `PICO OTA`.
3. Connect to the ESP bridge over Wi-Fi or the local network.
4. Open the dedicated Pico OTA page served by the ESP.
5. Upload [pico_powerstation_main_slot_b.bin](../release/1.0/pico_powerstation_main_slot_b.bin).
6. Wait for ESP staging and UART transfer to complete.
7. After completion, Pico should reboot into the normal runtime.

## 5. Returning to normal mode without OTA

If you only want to leave recovery:

1. Open the boot menu with `Down` during startup.
2. Select `BOOT MAIN`.

## 6. Returning to USB BOOTSEL

If OTA is unavailable or a full recovery is needed:

1. Open the boot menu with `Down` during startup.
2. Select `USB BOOTSEL`.
3. Then flash [combined_loader_recovery_main.uf2](../release/1.0/combined_loader_recovery_main.uf2).

## 7. What to use in the common service scenarios

- Full Pico recovery: `combined_loader_recovery_main.uf2`
- Pico OTA through ESP: `pico_powerstation_main_slot_b.bin`
- Recovery slot update only: `pico_powerstation_recovery_slot_a.bin`
- ESP bridge update: `esp32_s2_bridge_ota.bin` or a standard upload from `build/esp_upload`
