# Pico PowerStation OTA ESP: опис коду

## Призначення

`Pico PowerStation OTA ESP` це прошивка для системи на базі RP2040 + ESP32-S2, де:

- `RP2040 / Pico` керує живленням, BMS-логікою, захистами, UI та збереженням даних.
- `ESP32-S2 bridge` надає Wi-Fi, web UI, OTA для самої ESP і окремий канал OTA для Pico.

Поточна архітектура побудована за схемою:

- `loader`
- `slot A = recovery`
- `slot B = main`

Це означає, що основна робоча прошивка запускається із `slot B`, а `slot A` використовується як recovery-середовище для boot menu та OTA-оновлення Pico.

## Загальна архітектура

### Pico / RP2040

Ключові цілі збірки визначені в [CMakeLists.txt](../CMakeLists.txt):

- `pico_powerstation_loader` — окремий boot loader
- `pico_powerstation_slot_a` — recovery-збірка
- `pico_powerstation_slot_b` — основна робоча збірка
- `combined_loader_recovery_main.uf2` — повний UF2-образ для первинної прошивки Pico

Flash layout:

- `loader` розташований на початку flash
- `slot A` займає recovery-область
- `slot B` займає основну область прошивки

Loader визначений у [src/main_ota_loader.c](../src/main_ota_loader.c). Він:

- читає boot state з `boot_control`
- перевіряє валідність слотів
- підтримує примусовий boot у конкретний слот через watchdog scratch
- при утриманні кнопки `Down` під час старту запускає recovery `slot A`
- передає recovery-прошивці прапор для відкриття boot menu

### Boot menu

У recovery-збірці boot menu реалізоване в [src/main.c](../src/main.c).

Якщо під час вмикання утримується `Down`, користувач потрапляє не одразу в OTA, а в boot menu з трьома пунктами:

- `PICO OTA`
- `BOOT MAIN`
- `USB BOOTSEL`

Логіка така:

- `PICO OTA` залишає Pico у recovery-режимі для OTA через ESP
- `BOOT MAIN` виконує перезапуск через loader у `slot B`
- `USB BOOTSEL` переводить контролер у стандартний USB boot mode для прошивки через UF2

### Основна прошивка Pico

Основна логіка застосунку живе у:

- [src/app](../src/app)
- [src/bms](../src/bms)
- [src/drivers](../src/drivers)

Основні функціональні блоки:

- керування живленням і послідовністю старту
- BMS-логіка, оцінка SOC/SOH, статистика та накопичення даних
- читання датчиків струму, напруги та температури
- UI для дисплея ST7789
- журнали, settings і flash storage
- захисна логіка, обмеження та системні стани

Ключові модулі верхнього рівня:

- [src/app/boot_control.c](../src/app/boot_control.c) — структура boot state, вибір слотів, rollback і handoff між boot stages
- [src/app/ota_manager.c](../src/app/ota_manager.c) — приймання OTA на Pico і запис прошивки в `slot B`
- [src/app/esp_manager.c](../src/app/esp_manager.c) — UART-протокол між Pico та ESP
- [src/app/ui.c](../src/app/ui.c) — інтерфейс користувача, recovery/OTA екрани, системні стани
- [src/app/power_sequencer.c](../src/app/power_sequencer.c) — послідовність запуску й режими живлення

## ESP32-S2 bridge

ESP-частина знаходиться в [esp32_s2_bridge](../esp32_s2_bridge).

Вона виконує три ролі:

- web bridge для загального dashboard і налаштувань
- OTA для самої ESP
- окремий Pico OTA bridge для передачі `.bin` у Pico recovery

Основна логіка реалізована в [esp32_s2_bridge/src/main.cpp](../esp32_s2_bridge/src/main.cpp).

Ключові особливості:

- окремий режим `PICO OTA`
- окрема сторінка Pico OTA без доступу до загального dashboard під час recovery-сесії
- staging файлу прошивки Pico в `SPIFFS`
- асинхронна chunk-передача в Pico по UART
- вимкнений Wi-Fi sleep для стабільнішого OTA-каналу

UI розділений на:

- [esp32_s2_bridge/src/web_ui.h](../esp32_s2_bridge/src/web_ui.h) — загальний dashboard
- [esp32_s2_bridge/src/pico_ota_ui.h](../esp32_s2_bridge/src/pico_ota_ui.h) — окрема сторінка Pico OTA

## Механізм OTA Pico

Поточний потік оновлення Pico виглядає так:

1. Користувач утримує `Down` під час ввімкнення Pico.
2. Loader переводить систему в recovery `slot A` і відкриває boot menu.
3. У boot menu обирається `PICO OTA`.
4. ESP визначає, що Pico у recovery OTA mode, і відкриває окрему сторінку `/pico-ota`.
5. Файл `pico_powerstation_main_slot_b.bin` завантажується в ESP.
6. ESP спочатку повністю зберігає файл у staging (`SPIFFS`), а потім передає його в Pico chunk-by-chunk через UART.
7. Pico recovery приймає образ і записує його в `slot B`.
8. Після успішного завершення виконується перезапуск, loader перемикає систему в `slot B`.

## Актуальні артефакти

Поточний релізний комплект лежить у [release/1.0](../release/1.0):

- [combined_loader_recovery_main.uf2](../release/1.0/combined_loader_recovery_main.uf2) — повний образ Pico
- [pico_powerstation_recovery_slot_a.bin](../release/1.0/pico_powerstation_recovery_slot_a.bin) — recovery-slot
- [pico_powerstation_main_slot_b.bin](../release/1.0/pico_powerstation_main_slot_b.bin) — основна Pico-прошивка для OTA
- [esp32_s2_bridge_ota.bin](../release/1.0/esp32_s2_bridge_ota.bin) — актуальний ESP bridge application image

## Поточний стан релізу 1.0

Реліз `1.0` фіксує такий стан:

- стабілізована схема `loader + recovery slot A + main slot B`
- boot menu при утриманні `Down` під час старту
- відокремлений Pico OTA режим на ESP з окремою web-сторінкою
- зібрані й оновлені артефакти для Pico та ESP
- підтверджений сценарій: повна прошивка, перезапуск і повернення в робочий стан
