# Workplan / Context (2026-04-08)

## System overview (concise)
- **Pico / RP2040:** loader + slot A (recovery) + slot B (main). Веде BMS/захист, силові порти, UI на ST7789, зберігає логи/статистику/налаштування, приймає OTA від ESP.
- **ESP32-S2 bridge:** Wi‑Fi, web‑UI, OTA для себе, міст Pico OTA (SPIFFS staging → UART chunking). Окремі інтерфейси: загальна панель і сторінка Pico OTA.
- **Flash схема:** loader на початку, далі slot A (recovery), slot B (main), лог-сектора під кінець; розміри вираховуються від `FLASH_TOTAL` у CMake.

## Детальний опис системи (структуровано)
### Компоненти
- **Boot loader (Pico):** читає boot_control, вибирає слот, підтримує watchdog scratch для хенд‑оффу та меню recovery.
- **Slot A (recovery):** меню PICO OTA / BOOT MAIN / USB BOOTSEL; утримує UI й OTA-прийом у безпечному режимі.
- **Slot B (main):** робоча прошивка: BMS логіка, SOC/SOH оцінка, захисти, керування реле/вентилятором, логування та UI.
- **ESP32-S2 bridge:** мережа + OTA: 
  - web dashboard (telemetry, порти, налаштування),
  - OTA для ESP,
  - Pico OTA міст (приймає .bin, зберігає в SPIFFS, шле шматками по UART у recovery).

### Потоки даних
- **Пико ↔ ESP UART:** hello/telemetry/stats/settings/log slices/OTA chunks; час передається командою `SET time <epoch>`.
- **Web UI:** читає REST endpoints ESP (`/api/telemetry`, `/api/system`, `/api/stats`, `/api/log`, `/api/settings/*`, `/api/ota`), відображає стан та відправляє команди.
- **Логи/події (Pico):** timestamp — uptime, якщо немає epoch; при `SET time` переходять на реальний час.

### OTA/Boot flow (Pico)
1. Затиснута Down → loader стартує slot A (recovery) і показує меню.
2. У меню PICO OTA → ESP відкриває сторінку `/pico-ota`.
3. Користувач заливає `pico_powerstation_slot_b.bin` на ESP; той пише у SPIFFS і шле chunks на Pico.
4. Pico записує slot B, перевіряє, перезавантажується; loader підтверджує слот.

### Збереження
- **Flash NVM:** налаштування, логи, статистика, реле‑стани; офсети секторів задаються у `config.h` через `FLASH_TOTAL`.
- **SPIFFS (ESP):** staging OTA Pico, власні файли веб‑UI.

### Час/NTP
- **ESP:** трекає NTP sync; показує в статус‑барі (NTP: час/NO/OFF) і Bridge Info; надсилає `SET time` на Pico тільки при валідному NTP (epoch > 1700000000); fallback — усе працює на uptime.
- **Pico UI:** бейдж «SYNC/NO» у бездротовій панелі; логи відображають epoch час, якщо він є, інакше uptime.

### UI (актуальні акценти)
- **ESP web:** дуги SOC/Power спрощені (заливка + мітки 0/50/100), великі цифри центром; табличний Event Log.
- **Pico ST7789:** статус‑стрічка містить NTP badge; інші екрани без змін логіки.

## Project layout (корінь `C:\pico\Powerstation_OTA_2`)
- `src/` — RP2040 код (app/bms/drivers, main.c, loader, slots).
- `esp32_s2_bridge/` — ESP32-S2 прошивка (main.cpp, web UI headers, SPIFFS assets).
- `docs/` — описи/гайди (CODE_DESCRIPTION_EN.md, FLASHING_GUIDE*, CODE_DESCRIPTION_UK.md).
- `tools/` — скрипти для комбінованих UF2.
- `ui_assets/` — графіка UI.
- `build/` — поточні артефакти збірки (ninja).
- `OTA_ready/` — готові .bin/.uf2 для прошивання.
- `Unused/` — архів старих/проміжних збірок, кешів.
- `workplan.md` — цей контекст/план.

## Changes made (current session highlights)
- Очистка робочого дерева (Unused/) та повна збірка таргетів.
- ESP web UI:
  - Gauge → горизонтальні бари для SOC/Power (краще вміщення/читабельність).
  - Табличний Event Log.
  - NTP статус у статус‑барі/Bridge Info.
- Pico UI:
  - Бейдж NTP на screensaver (SYNC/NO).
  - ETA у screensaver для charge/discharge: показує “XH XM … · ETA HH:MM” при наявності реального часу.
- OTA артефакти (актуальні на момент сесії):
  - `OTA_ready/esp32_s2_bridge.bin`
  - `OTA_ready/pico_powerstation_slot_b.bin`

## Notes for future work
- ESP UI gauges/colors easily tweakable in `esp32_s2_bridge/src/web_ui.h`.
- Pico NTP badge uses `log_has_epoch()`: ensure ESP sends `SET time <epoch>` when NTP is valid.
- Logs store epoch when available; UI shows uptime-style age otherwise—can extend with absolute date/time if needed.
- HTTP 504 при /api/log: зменшено batch до 8; якщо повториться — зменшити запитаний count або сторінкувати.
- ETA у Pico: використовує epoch + константний TZ offset (+3h) для локального часу; якщо деплой в іншій TZ — змінити `ETA_TZ_OFFSET_SEC` у `config.h`.

## Update 2026-04-08 (log transport fix)
- Pico: _send_log_slice now buffers events, skips corrupt entries, and sets sent=factual to prevent ESP timeouts.
- ESP: Event log page size set to 8 to match UART batch; log UI remains tabular.
- OTA binaries rebuilt: OTA_ready/esp32_s2_bridge.bin and OTA_ready/pico_powerstation_slot_b.bin (log fix).
- Next steps: if /api/log still 504 -> clear/rewrite bad log entries or add paging; adjust TZ offset in config.h if deployment TZ differs.

