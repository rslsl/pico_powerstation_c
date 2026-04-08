cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pico_powerstation C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(POWERSTATION_FLASH_16MB "Build for 16MB flash modules" OFF)
option(POWERSTATION_USB_BRINGUP "Skip power latch for USB-only bring-up" OFF)

pico_sdk_init()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
  -Wall -Wextra
  -Wno-unused-parameter
  -ffunction-sections
  -fdata-sections
)

set(POWERSTATION_APP_SOURCES
  src/main.c
  src/drivers/tca9548a.c
  src/drivers/ina226.c
  src/drivers/ina3221.c
  src/drivers/lm75a.c
  src/drivers/st7789.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT_Font.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT_graphics.cpp
  src/third_party/ST7789_TFT_PICO/src/st7789/ST7789_TFT_Print.cpp
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
  src/app/power_control.c
  src/app/protection.c
  src/app/buzzer.c
  src/app/display.c
  src/app/boot_control.c
  src/app/ota_manager.c
  src/app/esp_manager.c
  src/app/save_manager.c
  src/app/session_manager.c
  src/app/system_settings.c
  src/app/ui_assets.c
  src/app/ui.c
)

set(POWERSTATION_APP_INCLUDE_DIRS
  src
  src/drivers
  src/bms
  src/app
  src/third_party/ST7789_TFT_PICO/include
)

set(POWERSTATION_APP_LIBS
  pico_stdlib
  pico_multicore
  hardware_i2c
  hardware_spi
  hardware_uart
  hardware_dma
  hardware_pwm
  hardware_gpio
  hardware_watchdog
  hardware_flash
  hardware_sync
  hardware_timer
)

if(POWERSTATION_FLASH_16MB)
  set(POWERSTATION_FLASH_BYTES 16777216)
  set(POWERSTATION_FW_TAG "v1.2.0-16M")
else()
  set(POWERSTATION_FLASH_BYTES 2097152)
  set(POWERSTATION_FW_TAG "v1.2.0-2M")
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

math(EXPR POWERSTATION_FLASH_SECTOR_SIZE "4096")
math(EXPR POWERSTATION_FLASH_LOG_OFFSET "${POWERSTATION_FLASH_BYTES} - (72 * ${POWERSTATION_FLASH_SECTOR_SIZE})")
math(EXPR POWERSTATION_OTA_LOADER_SIZE "(128 * 1024)")
math(EXPR POWERSTATION_OTA_APP_REGION_OFFSET "${POWERSTATION_OTA_LOADER_SIZE}")
math(EXPR POWERSTATION_OTA_APP_REGION_SIZE "${POWERSTATION_FLASH_LOG_OFFSET} - ${POWERSTATION_OTA_APP_REGION_OFFSET}")
math(EXPR POWERSTATION_OTA_SLOT_SIZE "${POWERSTATION_OTA_APP_REGION_SIZE} / 2")
math(EXPR POWERSTATION_OTA_SLOT_A_OFFSET "${POWERSTATION_OTA_APP_REGION_OFFSET}")
math(EXPR POWERSTATION_OTA_SLOT_B_OFFSET "${POWERSTATION_OTA_SLOT_A_OFFSET} + ${POWERSTATION_OTA_SLOT_SIZE}")

set(POWERSTATION_SDK_MEMMAP "${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2040/memmap_default.ld")
set(POWERSTATION_MEMMAP_TEMPLATE "${CMAKE_BINARY_DIR}/powerstation_memmap_offset.ld.in")
file(READ "${POWERSTATION_SDK_MEMMAP}" POWERSTATION_MEMMAP_CONTENT)
string(REPLACE
  "    INCLUDE \"pico_flash_region.ld\""
  "    FLASH(rx) : ORIGIN = @POWERSTATION_FLASH_ORIGIN@, LENGTH = @POWERSTATION_FLASH_LENGTH@"
  POWERSTATION_MEMMAP_CONTENT
  "${POWERSTATION_MEMMAP_CONTENT}"
)
file(WRITE "${POWERSTATION_MEMMAP_TEMPLATE}" "${POWERSTATION_MEMMAP_CONTENT}")

function(powerstation_generate_offset_linker_script OUT_VAR NAME OFFSET SIZE)
  math(EXPR POWERSTATION_FLASH_ORIGIN "0x10000000 + ${OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
  math(EXPR POWERSTATION_FLASH_LENGTH "${SIZE}" OUTPUT_FORMAT HEXADECIMAL)
  set(_script "${CMAKE_BINARY_DIR}/${NAME}.ld")
  configure_file("${POWERSTATION_MEMMAP_TEMPLATE}" "${_script}" @ONLY)
  set(${OUT_VAR} "${_script}" PARENT_SCOPE)
endfunction()

function(powerstation_configure_app_target TARGET FW_TAG SLOT_ID)
  target_include_directories(${TARGET} PRIVATE ${POWERSTATION_APP_INCLUDE_DIRS})
  target_link_libraries(${TARGET} PRIVATE ${POWERSTATION_APP_LIBS})
  target_compile_definitions(${TARGET} PRIVATE
    PICO_FLASH_SIZE_BYTES=${POWERSTATION_FLASH_BYTES}
    FLASH_TOTAL=${POWERSTATION_FLASH_BYTES}
    FW_VERSION="${FW_TAG}"
    DEBUG_USB_BRINGUP=${POWERSTATION_USB_BRINGUP_VALUE}
    PICO_OTA_CURRENT_SLOT=${SLOT_ID}
  )
  pico_enable_stdio_usb(${TARGET} 1)
  pico_enable_stdio_uart(${TARGET} 0)
  pico_add_extra_outputs(${TARGET})
  target_link_options(${TARGET} PRIVATE -Wl,--gc-sections)
endfunction()

powerstation_generate_offset_linker_script(
  POWERSTATION_LOADER_LD
  pico_powerstation_loader_memmap
  0
  ${POWERSTATION_OTA_LOADER_SIZE}
)
powerstation_generate_offset_linker_script(
  POWERSTATION_SLOT_A_LD
  pico_powerstation_slot_a_memmap
  ${POWERSTATION_OTA_SLOT_A_OFFSET}
  ${POWERSTATION_OTA_SLOT_SIZE}
)
powerstation_generate_offset_linker_script(
  POWERSTATION_SLOT_B_LD
  pico_powerstation_slot_b_memmap
  ${POWERSTATION_OTA_SLOT_B_OFFSET}
  ${POWERSTATION_OTA_SLOT_SIZE}
)

add_executable(pico_powerstation ${POWERSTATION_APP_SOURCES})
powerstation_configure_app_target(pico_powerstation "${POWERSTATION_FW_TAG}" 0)
if(POWERSTATION_FLASH_16MB)
  pico_set_linker_script(pico_powerstation ${CMAKE_SOURCE_DIR}/memmap_16mb.ld)
endif()

add_executable(pico_powerstation_slot_a ${POWERSTATION_APP_SOURCES})
powerstation_configure_app_target(pico_powerstation_slot_a "${POWERSTATION_FW_TAG}-recovery-a" 1)
target_compile_definitions(pico_powerstation_slot_a PRIVATE
  POWERSTATION_RECOVERY_SLOT=1
)
pico_set_linker_script(pico_powerstation_slot_a ${POWERSTATION_SLOT_A_LD})

add_executable(pico_powerstation_slot_b ${POWERSTATION_APP_SOURCES})
powerstation_configure_app_target(pico_powerstation_slot_b "${POWERSTATION_FW_TAG}-main-b" 2)
target_compile_definitions(pico_powerstation_slot_b PRIVATE
  POWERSTATION_RECOVERY_SLOT=0
)
pico_set_linker_script(pico_powerstation_slot_b ${POWERSTATION_SLOT_B_LD})

option(POWERSTATION_OTA_LOADER_DEBUG "Enable USB debug console in OTA loader" OFF)
if(POWERSTATION_OTA_LOADER_DEBUG)
  set(POWERSTATION_LOADER_USB_DEBUG_VALUE 1)
else()
  set(POWERSTATION_LOADER_USB_DEBUG_VALUE 0)
endif()

add_executable(pico_powerstation_loader
  src/main_ota_loader.c
  src/app/boot_control.c
)
target_include_directories(pico_powerstation_loader PRIVATE
  src
  src/app
)
target_link_libraries(pico_powerstation_loader PRIVATE
  pico_stdlib
  hardware_flash
  hardware_irq
  hardware_sync
  hardware_resets
  pico_bootrom
)
target_compile_definitions(pico_powerstation_loader PRIVATE
  PICO_FLASH_SIZE_BYTES=${POWERSTATION_FLASH_BYTES}
  FLASH_TOTAL=${POWERSTATION_FLASH_BYTES}
  FW_VERSION="${POWERSTATION_FW_TAG}-loader"
  DEBUG_USB_BRINGUP=0
  PICO_OTA_LOADER_USB_DEBUG=${POWERSTATION_LOADER_USB_DEBUG_VALUE}
  BOOTCTL_DIRECT_FLASH=1
)
pico_enable_stdio_usb(pico_powerstation_loader ${POWERSTATION_LOADER_USB_DEBUG_VALUE})
pico_enable_stdio_uart(pico_powerstation_loader 0)
pico_set_linker_script(pico_powerstation_loader ${POWERSTATION_LOADER_LD})
pico_add_extra_outputs(pico_powerstation_loader)
target_link_options(pico_powerstation_loader PRIVATE -Wl,--gc-sections)

add_executable(periph_test_new_pinout
  src/main_periph_test_new_pinout.c
)
target_link_libraries(periph_test_new_pinout PRIVATE
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
target_link_libraries(main_cpp_display_test PRIVATE
  pico_stdlib
  hardware_spi
)
pico_enable_stdio_usb(main_cpp_display_test 1)
pico_enable_stdio_uart(main_cpp_display_test 0)
pico_add_extra_outputs(main_cpp_display_test)

# ── Tiny slot test (minimal OTA chainload validation) ───────────
add_executable(pico_tiny_slot_test_a
  src/main_tiny_slot_test.c
  src/app/boot_control.c
)
target_include_directories(pico_tiny_slot_test_a PRIVATE
  src
  src/app
  src/bms
)
target_link_libraries(pico_tiny_slot_test_a PRIVATE
  pico_stdlib
  hardware_flash
  hardware_sync
  hardware_gpio
  hardware_watchdog
)
target_compile_definitions(pico_tiny_slot_test_a PRIVATE
  PICO_FLASH_SIZE_BYTES=${POWERSTATION_FLASH_BYTES}
  FLASH_TOTAL=${POWERSTATION_FLASH_BYTES}
  FW_VERSION="${POWERSTATION_FW_TAG}-tiny-a"
  PICO_OTA_CURRENT_SLOT=1
  DEBUG_USB_BRINGUP=0
  BOOTCTL_DIRECT_FLASH=1
  TINY_TEST_BOOTSEL_AFTER_LOOPS=20
)
pico_enable_stdio_usb(pico_tiny_slot_test_a 1)
pico_enable_stdio_uart(pico_tiny_slot_test_a 0)
pico_set_linker_script(pico_tiny_slot_test_a ${POWERSTATION_SLOT_A_LD})
pico_add_extra_outputs(pico_tiny_slot_test_a)
target_link_options(pico_tiny_slot_test_a PRIVATE -Wl,--gc-sections)

# ── Combined UF2 images (loader + slot) ─────────────────────────
find_program(PYTHON3 python3 python)
if(PYTHON3)
  set(UF2_COMBINE_SCRIPT "${CMAKE_SOURCE_DIR}/tools/build_combined_uf2.py")

  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/combined_loader_tiny_a.uf2
    COMMAND ${PYTHON3} ${UF2_COMBINE_SCRIPT}
      $<TARGET_FILE:picotool>
      ${CMAKE_BINARY_DIR}/combined_loader_tiny_a.uf2
      0xE48BFF56
      0x10000000
      ${CMAKE_BINARY_DIR}/pico_powerstation_loader.bin 0x0
      ${CMAKE_BINARY_DIR}/pico_tiny_slot_test_a.bin ${POWERSTATION_OTA_SLOT_A_OFFSET}
    DEPENDS pico_powerstation_loader pico_tiny_slot_test_a ${UF2_COMBINE_SCRIPT}
    COMMENT "Building combined loader + tiny_slot_test_a UF2"
  )
  add_custom_target(combined_loader_tiny_a ALL
    DEPENDS ${CMAKE_BINARY_DIR}/combined_loader_tiny_a.uf2
  )

  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/combined_loader_slot_a.uf2
    COMMAND ${PYTHON3} ${UF2_COMBINE_SCRIPT}
      $<TARGET_FILE:picotool>
      ${CMAKE_BINARY_DIR}/combined_loader_slot_a.uf2
      0xE48BFF56
      0x10000000
      ${CMAKE_BINARY_DIR}/pico_powerstation_loader.bin 0x0
      ${CMAKE_BINARY_DIR}/pico_powerstation_slot_a.bin ${POWERSTATION_OTA_SLOT_A_OFFSET}
    DEPENDS pico_powerstation_loader pico_powerstation_slot_a ${UF2_COMBINE_SCRIPT}
    COMMENT "Building combined loader + slot_a UF2"
  )
  add_custom_target(combined_loader_slot_a ALL
    DEPENDS ${CMAKE_BINARY_DIR}/combined_loader_slot_a.uf2
  )

  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/combined_loader_recovery_main.uf2
    COMMAND ${PYTHON3} ${UF2_COMBINE_SCRIPT}
      $<TARGET_FILE:picotool>
      ${CMAKE_BINARY_DIR}/combined_loader_recovery_main.uf2
      0xE48BFF56
      0x10000000
      ${CMAKE_BINARY_DIR}/pico_powerstation_loader.bin 0x0
      ${CMAKE_BINARY_DIR}/pico_powerstation_slot_a.bin ${POWERSTATION_OTA_SLOT_A_OFFSET}
      ${CMAKE_BINARY_DIR}/pico_powerstation_slot_b.bin ${POWERSTATION_OTA_SLOT_B_OFFSET}
    DEPENDS pico_powerstation_loader pico_powerstation_slot_a pico_powerstation_slot_b ${UF2_COMBINE_SCRIPT}
    COMMENT "Building combined loader + recovery slot_a + main slot_b UF2"
  )
  add_custom_target(combined_loader_recovery_main ALL
    DEPENDS ${CMAKE_BINARY_DIR}/combined_loader_recovery_main.uf2
  )
else()
  message(WARNING "Python3 not found; combined UF2 targets disabled")
endif()
