cmake_minimum_required(VERSION 3.13)

# ── YD-RP2040 / WeAct RP2040 — 16 МБ Flash build ──────────────
# Підключення Pico SDK (шлях задається через PICO_SDK_PATH або cmake preset)
include(pico_sdk_import.cmake)

project(pico_powerstation C CXX ASM)
set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)
option(POWERSTATION_FLASH_16MB "Build for 16MB flash modules" OFF)
option(POWERSTATION_USB_BRINGUP "Skip power latch for USB-only bring-up" OFF)

# Ініціалізація Pico SDK
pico_sdk_init()

# Оптимізація: Release = -O2, Debug = -O0 -g3
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
  -Wall -Wextra
  -Wno-unused-parameter
  -ffunction-sections -fdata-sections   # дозволяє --gc-sections
)

# ─── Файли проєкту ─────────────────────────────────────────────────
add_executable(pico_powerstation
  src/main.c

  # Драйвери периферії
  src/drivers/tca9548a.c
  src/drivers/ina226.c
  src/drivers/ina3221.c
  src/drivers/lm75a.c
  src/drivers/st7789.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT_Font.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT_graphics.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT_Print.cpp

  # BMS алгоритми
  src/bms/bms_ocv.c
  src/bms/bms_ekf.c
  src/bms/bms_rint.c
  src/bms/bms_soh.c
  src/bms/bms_predictor.c
  src/bms/bms_logger.c
    src/bms/bms_stats.c
    src/bms/flash_nvm.c
    src/app/power_sequencer.c
  src/bms/battery.c

  # Застосунок
  src/app/power_control.c
  src/app/protection.c
  src/app/buzzer.c
  src/app/display.c
  src/app/ui_assets.c
  src/app/ui.c
)

# Include paths
target_include_directories(pico_powerstation PRIVATE
  src
  src/drivers
  src/bms
  src/app
  src/third_party/ST7789_TFT_PICO/include
)

# Build profile
if(POWERSTATION_FLASH_16MB)
  set(POWERSTATION_FLASH_BYTES 16777216)
  set(POWERSTATION_FW_TAG "v4.0-C-16M")
else()
  set(POWERSTATION_FLASH_BYTES 2097152)
  set(POWERSTATION_FW_TAG "v4.0-C-2M")
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" POWERSTATION_BUILD_TYPE)
set(POWERSTATION_PRODUCTION_BUILD OFF)
if(POWERSTATION_BUILD_TYPE STREQUAL "RELEASE"
   OR POWERSTATION_BUILD_TYPE STREQUAL "RELWITHDEBINFO"
   OR POWERSTATION_BUILD_TYPE STREQUAL "MINSIZEREL")
  set(POWERSTATION_PRODUCTION_BUILD ON)
endif()

if(POWERSTATION_PRODUCTION_BUILD)
  set(POWERSTATION_USB_BRINGUP_VALUE 0)
  if(POWERSTATION_USB_BRINGUP)
    message(WARNING "POWERSTATION_USB_BRINGUP is ignored for production builds; DEBUG_USB_BRINGUP stays OFF")
  endif()
elseif(POWERSTATION_USB_BRINGUP)
  set(POWERSTATION_USB_BRINGUP_VALUE 1)
else()
  set(POWERSTATION_USB_BRINGUP_VALUE 0)
endif()

target_compile_definitions(pico_powerstation PRIVATE
  PICO_FLASH_SIZE_BYTES=${POWERSTATION_FLASH_BYTES}
  FLASH_TOTAL=${POWERSTATION_FLASH_BYTES}
  FW_VERSION="${POWERSTATION_FW_TAG}"
  DEBUG_USB_BRINGUP=${POWERSTATION_USB_BRINGUP_VALUE}
)

# Pico SDK бібліотеки
target_link_libraries(pico_powerstation
  pico_stdlib           # GPIO, UART, time
  pico_multicore        # Core1 support
  hardware_i2c          # I2C0
  hardware_spi          # SPI0 (ST7789)
  hardware_uart         # UART1 debug console
  hardware_dma          # DMA для SPI
  hardware_pwm          # PWM підсвітки
  hardware_gpio         # IRQ для енкодера
  hardware_watchdog     # WDT
  hardware_flash        # Flash NVM для логів
  hardware_sync         # spin_lock між ядрами
  hardware_timer        # hardware alarm timers
)

# USB Serial для printf/debug
pico_enable_stdio_usb(pico_powerstation 1)
pico_enable_stdio_uart(pico_powerstation 0)

# Custom linker script для 16 МБ Flash (YD-RP2040 / WeAct RP2040)
if(POWERSTATION_FLASH_16MB)
  pico_set_linker_script(pico_powerstation ${CMAKE_SOURCE_DIR}/memmap_16mb.ld)
endif()

# Генерація .uf2
pico_add_extra_outputs(pico_powerstation)

# Оптимізація розміру (видалити невикористані секції)
target_link_options(pico_powerstation PRIVATE
  -Wl,--gc-sections
)

# Minimal standalone peripheral smoke test for the new pinout.
add_executable(periph_test_new_pinout
  src/main_periph_test_new_pinout.c
)

target_link_libraries(periph_test_new_pinout
  pico_stdlib
  hardware_spi
  hardware_i2c
  hardware_pwm
)

target_include_directories(periph_test_new_pinout PRIVATE
  src
)

pico_enable_stdio_usb(periph_test_new_pinout 1)
pico_enable_stdio_uart(periph_test_new_pinout 0)
pico_add_extra_outputs(periph_test_new_pinout)

# C++ ST7789 smoke test based on user-provided main.cpp
add_executable(main_cpp_display_test
  src/main_cpp_display_test.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT_Font.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT_graphics.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT_Print.cpp
)

target_include_directories(main_cpp_display_test PRIVATE
  src/third_party/ST7789_TFT_PICO/include
)

target_link_libraries(main_cpp_display_test
  pico_stdlib
  hardware_spi
)

pico_enable_stdio_usb(main_cpp_display_test 1)
pico_enable_stdio_uart(main_cpp_display_test 0)
pico_add_extra_outputs(main_cpp_display_test)
