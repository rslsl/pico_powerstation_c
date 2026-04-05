# Pico PowerStation OTA ESP: інструкція з прошивки

## Готові файли

Актуальні релізні артефакти знаходяться в [release/1.0](../release/1.0):

- [combined_loader_recovery_main.uf2](../release/1.0/combined_loader_recovery_main.uf2)
- [pico_powerstation_recovery_slot_a.bin](../release/1.0/pico_powerstation_recovery_slot_a.bin)
- [pico_powerstation_main_slot_b.bin](../release/1.0/pico_powerstation_main_slot_b.bin)
- [esp32_s2_bridge_ota.bin](../release/1.0/esp32_s2_bridge_ota.bin)

## 1. Повна прошивка Pico через UF2

Використовуйте цей спосіб для первинної прошивки або повного відновлення.

Кроки:

1. Вимкніть Pico.
2. Натисніть і утримуйте `BOOTSEL`.
3. Підключіть Pico до ПК через USB.
4. У системі з'явиться диск `RPI-RP2`.
5. Скопіюйте на нього [combined_loader_recovery_main.uf2](../release/1.0/combined_loader_recovery_main.uf2).
6. Дочекайтеся завершення копіювання та автоматичного перезапуску плати.

Результат:

- буде прошито `loader`
- буде прошито recovery `slot A`
- буде прошито main `slot B`

## 2. Прошивка ESP32-S2 bridge

### Варіант A: стандартний upload із підготовленого build

Це рекомендований спосіб для робочого проєкту.

Потрібно:

- підключена ESP32-S2
- зібрана директорія `build/esp_upload`
- `arduino-cli`

Приклад команди:

```powershell
"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" upload `
  -p COM16 `
  --fqbn esp32:esp32:lolin_s2_mini `
  --input-dir C:\pico\pico_powerstation_c\build\esp_upload `
  -v
```

Цей шлях записує повний комплект, сумісний із поточною збіркою bridge.

### Варіант B: використання готового application image

[esp32_s2_bridge_ota.bin](../release/1.0/esp32_s2_bridge_ota.bin) — це актуальний application image bridge.

Його слід використовувати лише у тих сценаріях, де bootloader і partition table на ESP уже відповідають поточній конфігурації проєкту.

## 3. Вхід у boot menu Pico

Тепер `Down` на старті не запускає OTA одразу.

Щоб відкрити boot menu:

1. Повністю вимкніть систему.
2. Затисніть `Down`.
3. Увімкніть живлення, продовжуючи тримати кнопку.
4. Дочекайтесь появи boot menu.

У boot menu доступні:

- `PICO OTA`
- `BOOT MAIN`
- `USB BOOTSEL`

## 4. OTA-оновлення Pico через ESP

Цей сценарій використовується для оновлення лише `slot B`.

Кроки:

1. Увійдіть у boot menu Pico.
2. Оберіть `PICO OTA`.
3. Підключіться до ESP bridge через Wi-Fi або локальну мережу.
4. Відкрийте окрему Pico OTA сторінку ESP.
5. Завантажте файл [pico_powerstation_main_slot_b.bin](../release/1.0/pico_powerstation_main_slot_b.bin).
6. Дочекайтесь завершення staging на ESP і передачі в Pico.
7. Після завершення Pico має перезавантажитися в основний робочий стан.

## 5. Повернення в нормальний режим без OTA

Якщо треба просто вийти з recovery:

1. Відкрийте boot menu через `Down` при старті.
2. Оберіть `BOOT MAIN`.

## 6. Повернення в USB BOOTSEL

Якщо OTA недоступне або потрібне повне відновлення:

1. Відкрийте boot menu через `Down` при старті.
2. Оберіть `USB BOOTSEL`.
3. Після цього прошийте [combined_loader_recovery_main.uf2](../release/1.0/combined_loader_recovery_main.uf2).

## 7. Що використовувати в типовому сервісному сценарії

- Для повного відновлення Pico: `combined_loader_recovery_main.uf2`
- Для OTA Pico через ESP: `pico_powerstation_main_slot_b.bin`
- Для оновлення recovery окремо: `pico_powerstation_recovery_slot_a.bin`
- Для bridge ESP: `esp32_s2_bridge_ota.bin` або стандартний upload із `build/esp_upload`
