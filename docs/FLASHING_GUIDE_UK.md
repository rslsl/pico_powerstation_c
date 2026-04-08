# PowerStation OTA ESP: Інструкція з прошивки

## Актуальні готові файли

Поточні файли для прошивки знаходяться в `../OTA_ready/`:

- `combined_loader_recovery_main.uf2`
- `pico_powerstation_slot_b.bin`
- `esp32_s2_bridge.bin`

Саме ці артефакти відповідають поточній структурі репозиторію та актуальному коду.

## 1. Повна прошивка Pico / відновлення

Використовуйте цей спосіб для:

- першої прошивки
- повного відновлення пристрою
- запису `loader + slot A + slot B` за один раз

Файл:

- `../OTA_ready/combined_loader_recovery_main.uf2`

Кроки:

1. Вимкніть Pico.
2. Затисніть `BOOTSEL`.
3. Підключіть Pico по USB.
4. Дочекайтеся появи диска `RPI-RP2`.
5. Скопіюйте `combined_loader_recovery_main.uf2` на цей диск.
6. Дочекайтеся завершення копіювання та автоматичного перезапуску плати.

Результат:

- буде прошито `loader`
- буде прошито recovery `slot A`
- буде прошито main `slot B`

## 2. Прошивка ESP32-S2 bridge

### Варіант A: стандартний serial upload із поточного PlatformIO-проєкту

Це рекомендований спосіб, якщо ESP32-S2 підключена до ПК.

Збірка та upload:

```powershell
cd C:\pico\Powerstation_OTA_2\esp32_s2_bridge
python -m platformio run -t upload --upload-port COM16
```

Замініть `COM16` на реальний порт ESP.

### Варіант B: використання готового образу ESP

Файл:

- `../OTA_ready/esp32_s2_bridge.bin`

Цей варіант підходить, якщо:

- на пристрої вже сумісний bootloader і коректна partition layout
- ви оновлюєте ESP через наявний OTA-механізм або інший перевірений спосіб запису

## 3. Вхід у boot menu Pico

Тепер `Down` не запускає OTA напряму. Він відкриває recovery boot menu.

Кроки:

1. Повністю вимкніть систему.
2. Затисніть `Down`.
3. Увімкніть живлення, продовжуючи тримати кнопку.
4. Дочекайтеся появи boot menu.

У меню доступні:

- `PICO OTA`
- `BOOT MAIN`
- `USB BOOTSEL`

## 4. OTA-оновлення Pico через ESP

Цей сценарій оновлює тільки `slot B`.

Файл:

- `../OTA_ready/pico_powerstation_slot_b.bin`

Кроки:

1. Увійдіть у boot menu Pico.
2. Оберіть `PICO OTA`.
3. Підключіться до ESP bridge через Wi-Fi або локальну мережу.
4. Відкрийте окрему Pico OTA сторінку на ESP.
5. Завантажте `pico_powerstation_slot_b.bin`.
6. Дочекайтеся завершення staging на ESP і передачі в Pico.
7. Після завершення Pico має перезавантажитися в нормальний робочий режим.

## 5. Повернення в нормальний режим без OTA

Якщо потрібно просто вийти з recovery:

1. Відкрийте boot menu через `Down` під час старту.
2. Оберіть `BOOT MAIN`.

## 6. Повернення в USB BOOTSEL

Якщо OTA недоступне або потрібне повне відновлення:

1. Відкрийте boot menu через `Down` під час старту.
2. Оберіть `USB BOOTSEL`.
3. Прошийте `../OTA_ready/combined_loader_recovery_main.uf2`.

## 7. Типові сервісні сценарії

- Повне відновлення Pico: `combined_loader_recovery_main.uf2`
- OTA Pico через ESP: `pico_powerstation_slot_b.bin`
- Оновлення ESP bridge: `esp32_s2_bridge.bin` або serial upload із `esp32_s2_bridge`

## 8. Примітка щодо старих файлів

Старі release-папки та архівні артефакти більше не є основним джерелом актуальних прошивок.

Для поточної роботи використовуйте папку `OTA_ready/`.

Якщо потрібен окремий образ recovery `slot A` або інші нестандартні артефакти, їх слід перебирати з поточного `CMake`-проєкту, а не брати зі старих релізних папок.
