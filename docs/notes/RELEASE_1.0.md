# Release 1.0

Release date: `2026-04-05`

## Українська

`Release 1.0` фіксує перший повний робочий стан OTA-архітектури для зв'язки `Pico + ESP`.

Що входить у реліз:

- boot loader для Pico
- recovery `slot A`
- main runtime `slot B`
- boot menu при утриманні `Down` під час старту
- окремий Pico OTA режим на ESP з виділеною web-сторінкою
- актуальні артефакти прошивки для Pico та ESP

Ключові зміни релізу:

- архітектура змінена з runtime OTA mode усередині основної прошивки на схему `loader + recovery + main`
- `slot A` використовується як recovery-середовище
- `slot B` використовується як основна робоча прошивка
- OTA Pico ізольоване на окремій сторінці ESP без доступу до загального dashboard під час recovery-сесії
- додано boot menu з пунктами `PICO OTA`, `BOOT MAIN`, `USB BOOTSEL`
- підготовлено повний `UF2` для швидкого відновлення Pico

Релізні файли:

- [combined_loader_recovery_main.uf2](../release/1.0/combined_loader_recovery_main.uf2)
- [pico_powerstation_recovery_slot_a.bin](../release/1.0/pico_powerstation_recovery_slot_a.bin)
- [pico_powerstation_main_slot_b.bin](../release/1.0/pico_powerstation_main_slot_b.bin)
- [esp32_s2_bridge_ota.bin](../release/1.0/esp32_s2_bridge_ota.bin)

Супровідна документація:

- [Опис коду українською](./CODE_DESCRIPTION_UK.md)
- [Опис коду англійською](./CODE_DESCRIPTION_EN.md)
- [Інструкція з прошивки українською](./FLASHING_GUIDE_UK.md)
- [Flashing guide in English](./FLASHING_GUIDE_EN.md)

## English

`Release 1.0` captures the first complete working OTA architecture for the `Pico + ESP` combination.

Included in this release:

- Pico boot loader
- recovery `slot A`
- main runtime `slot B`
- boot menu when holding `Down` during startup
- dedicated Pico OTA mode on ESP with a separate web page
- current firmware artifacts for Pico and ESP

Key release changes:

- the architecture moved from a runtime OTA mode inside the main firmware to a `loader + recovery + main` layout
- `slot A` is used as the recovery environment
- `slot B` is used as the main production firmware
- Pico OTA is isolated on a dedicated ESP page with no access to the general dashboard during recovery sessions
- a boot menu was added with `PICO OTA`, `BOOT MAIN`, and `USB BOOTSEL`
- a full `UF2` image is provided for fast Pico recovery

Release files:

- [combined_loader_recovery_main.uf2](../release/1.0/combined_loader_recovery_main.uf2)
- [pico_powerstation_recovery_slot_a.bin](../release/1.0/pico_powerstation_recovery_slot_a.bin)
- [pico_powerstation_main_slot_b.bin](../release/1.0/pico_powerstation_main_slot_b.bin)
- [esp32_s2_bridge_ota.bin](../release/1.0/esp32_s2_bridge_ota.bin)

Supporting documentation:

- [Code description in Ukrainian](./CODE_DESCRIPTION_UK.md)
- [Code description in English](./CODE_DESCRIPTION_EN.md)
- [Flashing guide in Ukrainian](./FLASHING_GUIDE_UK.md)
- [Flashing guide in English](./FLASHING_GUIDE_EN.md)
