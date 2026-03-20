# PowerStation BMS v2.0-C — Build Guide

## Вимоги

| Інструмент | Версія |
|---|---|
| Raspberry Pi Pico SDK | ≥ 2.0.0 |
| CMake | ≥ 3.13 |
| ARM GCC Toolchain | arm-none-eabi-gcc 12+ |
| Python 3 | для Pico SDK scripts |

### Встановлення (Ubuntu/Debian)
```bash
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi \
                 build-essential libstdc++-arm-none-eabi-newlib
```

### Встановлення Pico SDK
```bash
git clone https://github.com/raspberrypi/pico-sdk.git --branch 2.0.0
cd pico-sdk && git submodule update --init
export PICO_SDK_PATH=$(pwd)
```

---

## Збірка

```bash
cd pico_powerstation_c

# Копіювати pico_sdk_import.cmake з SDK (або вже є у проєкті)
# cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

Результат: `build/pico_powerstation.uf2`

### Завантаження на Pico
```bash
# Утримуйте BOOTSEL при підключенні USB, потім:
cp build/pico_powerstation.uf2 /media/$USER/RPI-RP2/
```

---

## Структура проєкту

```
pico_powerstation_c/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── BUILD.md
└── src/
    ├── config.h                  ← всі константи, GPIO, пороги
    ├── main.c                    ← Core0 + Core1 точки входу
    │
    ├── drivers/                  ← HAL-незалежні драйвери
    │   ├── tca9548a.{h,c}        ← I2C MUX 1:8
    │   ├── ina226.{h,c}          ← V/I/P монітор (×2)
    │   ├── ina3221.{h,c}         ← 3-канальний моніторинг комірок
    │   ├── lm75a.{h,c}           ← датчики температури (×2)
    │   └── st7789.{h,c}          ← дисплей 240×280 + DMA flush
    │
    ├── bms/                      ← BMS алгоритми
    │   ├── bms_ocv.{h,c}         ← OCV таблиця + temp компенсація
    │   ├── bms_ekf.{h,c}         ← Extended Kalman Filter (SOC)
    │   ├── bms_rint.{h,c}        ← Thevenin 1RC модель (R0/R1/C1)
    │   ├── bms_soh.{h,c}         ← State of Health (EFC, RUL, Flash)
    │   ├── bms_predictor.{h,c}   ← прогноз часу (EMA+Window+Reg)
    │   ├── bms_logger.{h,c}      ← ring-buffer лог подій на Flash
    │   └── battery.{h,c}         ← центральний координатор BMS
    │
    └── app/                      ← застосунок
        ├── power_control.{h,c}   ← MOSFET active-LOW керування
        ├── protection.{h,c}      ← захисна логіка + bitmap tривог
        ├── buzzer.{h,c}          ← non-blocking buzzer (15 паттернів)
        ├── display.{h,c}         ← framebuffer + 8×8 шрифт
        └── ui.{h,c}              ← state machine UI + encoder ISR
```

---

## Архітектура виконання

```
Core0 (BMS + Protection + UI poll)          Core1 (Display)
────────────────────────────────────         ───────────────────────
loop @200us sleep_us(200):                   loop @100ms sleep_until:
  watchdog_update()          ← ПЕРШИЙ        disp_flush_wait()      ← DMA done?
  if t_sensor elapsed (200ms):               ui_render()             ← framebuffer
    bat_read_sensors()    ← I2C             disp_flush_async()      ← DMA start
  if t_logic elapsed (100ms):
    bat_update_bms()      ← EKF
    pred_update()         ← predictor
    prot_check()          ← protection
    buz alarms
  if t_save elapsed (5min):
    bat_save()            ← Flash
  ui_poll()               ← encoder ISR
  buz_tick()              ← pattern step

spin_lock захищає Battery struct між ядрами.
DMA-flush ST7789 не блокує жоден з ядер.
```

---

## Порівняння MicroPython → C

| Метрика | MicroPython v1.5 | C v2.0 |
|---|---|---|
| Доступна RAM | ~160 KB | 264 KB (всі) |
| EKF step() | ~8–15 ms | ~0.1–0.3 ms |
| Framebuffer flush | 27 ms (блокуючий) | ~0 ms (DMA async) |
| WDT feed | гарантований | апаратний, критичний шлях |
| Heap allocations | постійно (GC) | нуль у hot path |
| Flash wear (SOH) | кожні 5 хв | кожні 5 хв (однаково) |
| BMS цикл max freq | 10 Hz | 100 Hz (обмежено I2C) |
| Multicore | обмежений `_thread` | повноцінний SMP |
| I2C timeout | `I2C(timeout=50ms)` | `i2c_read_timeout_us` |

---

## Flash NVM Layout (2MB)

```
0x10000000  ┌─────────────────────────────┐ ← XIP base
            │  .text (код прошивки)        │
            │  .rodata (OCV таблиці тощо)  │
            │  .data / .bss               │
            │         ...                 │
0x101EC000  ├─────────────────────────────┤ ← FLASH_LOG_OFFSET
            │  Event log ring (48 KB)     │ ← 12 × 4KB sectors
            │  sector 0: header/index     │
            │  sectors 1-11: records×128  │
0x101F8000  ├─────────────────────────────┤
            │  History (future use)        │ ← 4KB
0x101FC000  ├─────────────────────────────┤
            │  SOH data (256 bytes used)  │ ← 4KB sector
0x101FD000  ├─────────────────────────────┤
            │  Settings (256 bytes)        │ ← 4KB sector
0x10200000  └─────────────────────────────┘ ← Flash end
```

---

## Налагодження (Debug build)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
# Debug build: -O0 -g3, WDT потрібно вимкнути або збільшити таймаут
# printf виводиться через USB CDC (stdio_usb)
```

## Рекомендовані наступні кроки

1. **Passive cell balancing** — 3 додаткові GPIO (GP18/GP19/GP20) → резистори + MOSFET на кожну комірку
2. **Pico W + MQTT** — замінити Pico на Pico W, додати telemetry через WiFi
3. **SD-карта** — SPI1 (GP10-13) для CSV логування всіх параметрів кожну секунду
4. **BQ76952 co-processor** — апаратний захист незалежно від MCU (для продуктового рівня)
5. **FreeRTOS** — якщо логіка ускладниться: priority tasks замість кооперативного планувальника
