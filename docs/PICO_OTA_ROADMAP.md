# Pico OTA Roadmap

## Goal

Add safe OTA firmware updates for the RP2040 PowerStation board through the existing ESP32-S2 bridge.

The target end state is:

- ESP web UI accepts a Pico firmware upload
- ESP streams the firmware to Pico over UART
- Pico writes the new image into the inactive slot
- boot control marks the new slot as pending
- a small OTA loader boots the pending slot
- the new firmware confirms boot success or the loader rolls back automatically

## Current Foundation

The codebase now contains:

- fixed OTA flash layout constants in `src/config.h`
- reserved loader region plus two Pico app slots
- boot-control A/B metadata design in `src/app/boot_control.[ch]`
- a dedicated Pico loader target at flash base
- slot-based Pico application targets for `slot A` and `slot B`
- boot confirmation from slot firmware after a successful startup path

Current runtime behavior:

- the legacy monolithic build still exists for fallback/manual work
- `Phase 2` boot flow now exists as `loader -> slot A/slot B`
- the combined `loader + slot A` image can already be flashed manually through `BOOTSEL`

## Flash Layout

### 2 MB flash build

- Loader reservation: `0x000000 .. 0x01FFFF` (`128 KB`)
- Boot control A: `0x01E000`
- Boot control B: `0x01F000`
- Slot A: `0x020000 .. 0x0EBFFF` (`0xCC000`, `835,584 bytes`)
- Slot B: `0x0EC000 .. 0x1B7FFF` (`0xCC000`, `835,584 bytes`)
- Existing NVM tail starts at `0x1B8000`

### 16 MB flash build

- Loader reservation: `0x000000 .. 0x01FFFF`
- Boot control A: `0x01E000`
- Boot control B: `0x01F000`
- Slot A: `0x020000 .. 0x7EBFFF` (`0x7CC000`)
- Slot B: `0x7EC000 .. 0xFB7FFF` (`0x7CC000`)
- Existing NVM tail starts at `0xFB8000`

## Phases

### Phase 1: Layout and Boot Metadata

- Reserve flash layout for loader and dual slots
- Add persistent boot metadata with A/B redundancy
- Add helpers for active slot, pending slot, confirmed slot and rollback state

Status: in progress

### Phase 2: Pico OTA Loader

- Add a minimal loader image at flash base
- Verify metadata and choose a boot slot
- Count boot attempts for pending images
- Roll back to the last confirmed image if the pending image does not confirm boot

Status: in progress
Implemented now:

- separate `pico_powerstation_loader` image linked at flash base
- vector-table validation before booting a slot
- pending-slot boot attempt counting
- rollback to confirmed/active slot when pending boot attempts are exhausted
- direct flash A/B metadata writes in loader without multicore lockout deadlock
 
Still next:

- confirm full rollback behavior against real failed slot boots
- hook loader state more tightly into the future UART OTA pipeline

### Phase 3: Slot-based Pico Application Builds

- Build the main PowerStation firmware for slot A or slot B
- Generate raw `.bin` artifacts suitable for OTA
- Keep `.uf2` for manual recovery via BOOTSEL

Status: in progress
Implemented now:

- `pico_powerstation_slot_a`
- `pico_powerstation_slot_b`
- current-slot self-confirm after successful boot
- combined manual recovery image generation for `loader + slot A`

### Phase 4: UART OTA Transport

- Add a binary chunked OTA protocol between ESP and Pico
- Support begin, write, finalize, abort and progress reporting
- Verify image size and CRC on Pico before marking the slot pending

### Phase 5: ESP Web OTA for Pico

- Add a separate upload flow for Pico firmware in the ESP web UI
- Show progress and result back on the PowerStation screen
- Prevent Pico OTA if battery or runtime state is unsafe

### Phase 6: Validation and Recovery

- Test brownout, watchdog reset and interrupted update scenarios
- Validate rollback after bad image or missing boot confirmation
- Document recovery procedure through BOOTSEL and USB

## Safety Rules

- Never overwrite the running Pico application in place
- Only write the inactive slot
- Only switch slots after full image verification
- Require explicit boot confirmation from the new image
- Roll back automatically after repeated failed boots
