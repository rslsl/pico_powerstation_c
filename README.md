# Pico PowerStation C

[Українська](#українська) | [English](#english)

## Українська

Прошивка для Raspberry Pi Pico / RP2040 для портативної power station з моніторингом батареї, захисною логікою, UI на ST7789 та модульною C/C++ кодовою базою.

Проєкт містить:

- BMS-логіку з оцінкою SOC/SOH
- драйвери сенсорів і периферії для RP2040
- UI для дисплея ST7789 240x280
- допоміжні build-цілі для перевірки дисплея та нового pinout


## Ключові особливості

- Двоядерна архітектура: Core0 виконує опитування сенсорів, BMS-логіку, захист і UI polling, а Core1 обслуговує дисплей.
- Модульна структура коду з поділом на `drivers`, `bms` і `app`.
- Конвеєр відмальовування ST7789 з DMA-орієнтованим підходом.
- Підтримка як основної firmware-цілі, так і окремих апаратних smoke test.

## Апаратний стек

- Raspberry Pi Pico / RP2040
- ST7789 240x280 display
- TCA9548A I2C multiplexer
- INA226 current / voltage monitors
- INA3221 multi-channel cell monitor
- LM75A temperature sensors

## Структура репозиторію

```text
pico_powerstation_c/
|- CMakeLists.txt
|- pico_sdk_import.cmake
|- memmap_16mb.ld
|- src/
|  |- app/          application logic, UI, power control, protection, buzzer
|  |- bms/          battery algorithms, prediction, logging, flash NVM
|  |- drivers/      hardware drivers
|  `- third_party/  ST7789 support library
|- ui_assets/       PNG assets, RGB565 headers, previews
|- tmp/pdfs/        helper script for PDF generation
`- BUILD.md         detailed build notes
```

## Основні цілі

- `pico_powerstation` - основна firmware-ціль
- `periph_test_new_pinout` - smoke test периферії для нового pinout
- `main_cpp_display_test` - smoke test дисплея ST7789

## Збірка

Потрібно:

- Raspberry Pi Pico SDK 2.x
- CMake 3.13+
- ARM GCC toolchain (`arm-none-eabi-gcc`)
- Python 3 для Pico SDK scripts

Типовий сценарій збірки:

```bash
git clone https://github.com/rslsl/pico_powerstation_c.git
cd pico_powerstation_c

# Make sure PICO_SDK_PATH points to your pico-sdk checkout
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

Основний результат:

```text
build/pico_powerstation.uf2
```

Для детальніших нотаток по збірці, flash layout і запуску дивись `BUILD.md`.

## Примітки

- Репозиторій зберігає вихідний код та UI-асети, а згенеровані build-артефакти ігноруються.
- `memmap_16mb.ld` доступний для збірок під 16 MB flash.

---

## English

Raspberry Pi Pico / RP2040 firmware for a portable power station with battery monitoring, protection logic, ST7789 UI, and a modular C/C++ codebase.

The project includes:

- BMS logic with SOC/SOH estimation
- sensor and peripheral drivers for the RP2040 platform
- a 240x280 ST7789-based UI
- auxiliary build targets for display and pinout smoke tests


## Highlights

- Dual-core layout: Core0 handles sensing, BMS logic, protection, and UI polling; Core1 is used for display work.
- Modular source tree split into `drivers`, `bms`, and `app`.
- ST7789 display pipeline with DMA-oriented rendering flow.
- Support for both the main firmware image and small standalone hardware tests.

## Hardware Stack

- Raspberry Pi Pico / RP2040
- ST7789 240x280 display
- TCA9548A I2C multiplexer
- INA226 current / voltage monitors
- INA3221 multi-channel cell monitor
- LM75A temperature sensors

## Repository Layout

```text
pico_powerstation_c/
|- CMakeLists.txt
|- pico_sdk_import.cmake
|- memmap_16mb.ld
|- src/
|  |- app/          application logic, UI, power control, protection, buzzer
|  |- bms/          battery algorithms, prediction, logging, flash NVM
|  |- drivers/      hardware drivers
|  `- third_party/  ST7789 support library
|- ui_assets/       PNG assets, RGB565 headers, previews
|- tmp/pdfs/        helper script for PDF generation
`- BUILD.md         detailed build notes
```

## Main Targets

- `pico_powerstation` - main firmware target
- `periph_test_new_pinout` - peripheral smoke test for the new pinout
- `main_cpp_display_test` - ST7789 display smoke test

## Build

Requirements:

- Raspberry Pi Pico SDK 2.x
- CMake 3.13+
- ARM GCC toolchain (`arm-none-eabi-gcc`)
- Python 3 for Pico SDK scripts

Typical build flow:

```bash
git clone https://github.com/rslsl/pico_powerstation_c.git
cd pico_powerstation_c

# Make sure PICO_SDK_PATH points to your pico-sdk checkout
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

Primary output:

```text
build/pico_powerstation.uf2
```

For more detailed notes, flash layout details, and build guidance, see `BUILD.md`.

## Notes

- The repository tracks source code and UI assets, while generated build outputs are ignored.
- `memmap_16mb.ld` is available for 16 MB flash builds when needed.
