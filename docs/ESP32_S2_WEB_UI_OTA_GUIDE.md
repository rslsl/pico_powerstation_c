# ESP32-S2 Web UI / OTA Guide

## Призначення

Цей документ описує нові можливості ESP32-S2 bridge для `pico_powerstation`, структуру web UI, сценарії використання OTA та правильний шлях збірки/прошивки через Arduino по USB.

## Що додано

### 1. Новий структурований web UI

Замість однієї довгої сторінки реалізовано багатосторінковий SPA-інтерфейс з окремими розділами:

- `Dashboard` — головна панель стану з ключовими метриками, індикаторами і швидкими діями
- `Telemetry` — живі параметри батареї, потужності, температур, графіки та progress bars
- `Events` — останні події, live log slice, розподіл подій по типах, експорт JSON/CSV
- `Settings` — налаштування RP2040, Wi-Fi профілю та OTA upload
- `System` — діагностика bridge, cache JSON, counters, ACK/ERROR дані

### 2. Покращений UI

Додано:

- адаптивну навігацію по розділах
- іконки для основних функціональних блоків
- progress bars для SOC, thermal state, link readiness, voltage/current/temp activity
- анімоване кільце SOC
- SVG-графіки для SOC, power і температур
- activity feed
- toast-сповіщення про дії та помилки
- OTA upload з progress bar

### 3. Розширена діагностика bridge

У `/api/system` додано службові лічильники:

- `hello_counter`
- `telemetry_counter`
- `stats_counter`
- `settings_counter`
- `ack_counter`
- `error_counter`

Ці поля використовуються на сторінці `System` для аналізу стану зв'язку між ESP32-S2 та RP2040.

## Основні змінені файли

- `esp32_s2_bridge/src/web_ui.h`
- `esp32_s2_bridge/src/main.cpp`

## Як працює web UI

### Відкриття інтерфейсу

Після старту ESP32-S2 відкрийте в браузері:

- IP точки доступу ESP, або
- IP, отриманий у режимі `STA`, якщо bridge підключений до роутера

Головні API:

- `/api/system`
- `/api/telemetry`
- `/api/stats`
- `/api/settings/rp2040`
- `/api/settings/network`
- `/api/log`
- `/api/cache`

Експорт:

- `/export/stats.json`
- `/export/stats.csv`
- `/export/log.json`
- `/export/log.csv`

## OTA сценарії

Підтримуються два варіанти OTA:

### 1. Browser OTA

1. На PowerStation переведіть bridge у режим `OTA`
2. Відкрийте сторінку `Settings`
3. У блоці `OTA Upload Deck` виберіть `.bin`
4. Натисніть `Upload OTA Binary`
5. Дочекайтесь завершення upload та reboot

### 2. ArduinoOTA

Коли RP2040 переводить bridge в режим `OTA`, на ESP також активується `ArduinoOTA`.

## Збірка через Arduino CLI

### Компіляція

```powershell
& "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile `
  --fqbn esp32:esp32:lolin_s2_mini:CDCOnBoot=default,PartitionScheme=default,DebugLevel=none,EraseFlash=none `
  --build-property "build.extra_flags=-DARDUINO_USB_CDC_ON_BOOT=1 -DPSTATION_UART_RX_PIN=16 -DPSTATION_UART_TX_PIN=17 -DPSTATION_UART_BAUD=115200" `
  --build-path "C:\pico\pico_powerstation\esp32_s2_bridge\build\arduino-cli" `
  "C:\pico\pico_powerstation\esp32_s2_bridge"
```

### Завантаження по USB через Arduino

```powershell
& "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" upload `
  --fqbn esp32:esp32:lolin_s2_mini:CDCOnBoot=default,PartitionScheme=default,DebugLevel=none,EraseFlash=none `
  --build-path "C:\pico\pico_powerstation\esp32_s2_bridge\build\arduino-cli" `
  --port COM16 `
  --protocol serial `
  --upload-property upload.speed=460800 `
  "C:\pico\pico_powerstation\esp32_s2_bridge"
```

## Важливо про USB-прошивку

Плата підключається через штатний USB-порт ESP32-S2, без окремого UART-адаптера.

Під час upload Arduino може тимчасово перевести плату з одного COM-порту на інший. Наприклад:

- робочий порт: `COM16`
- bootloader-порт під час reset: `COM15`

Це нормальна поведінка для native USB.

## Якщо upload не проходить

### Варіант 1. Простий повтор

1. Від'єднайте і знову під'єднайте USB
2. Перевірте актуальний COM-порт
3. Повторіть `arduino-cli upload`

### Варіант 2. Ручний вхід у bootloader

1. Затисніть кнопку `BOOT`
2. Коротко натисніть `RST`
3. Відпустіть `RST`
4. Відпустіть `BOOT`
5. Повторіть upload

### Варіант 3. Перевірка системою

Перевірка активного COM:

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name,Description
```

## Логіка взаємодії з RP2040

ESP32-S2:

- кешує телеметрію
- віддає web UI та REST API
- відправляє `STATUS` назад на RP2040
- обробляє команди `SET`, `ACTION`, `GET`
- приймає OTA upload тільки у валідному режимі

## Рекомендований порядок перевірки після прошивки

1. Переконатись, що ESP стартує і піднімає USB/COM
2. Відкрити web UI
3. Перевірити `Dashboard`
4. Перевірити оновлення `Telemetry`
5. Відкрити `System` і переконатись, що ростуть counters
6. Перевірити `Events`
7. Перевірити `Settings`
8. За потреби протестувати browser OTA

## Результат

Отримано окремий ESP32-S2 web interface з більш сучасною структурою, покращеним UX, розширеною діагностикою та готовим сценарієм OTA через browser і через Arduino/USB workflow.
