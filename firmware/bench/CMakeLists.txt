cmake_minimum_required(VERSION 3.20)
set(PICO_BOARD fuse_vault CACHE STRING "Board")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../src/platform/rp2354/boards")
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(fuse_vault_v2_bench C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()
set(FV_SD_CLOCK_HZ 25000000 CACHE STRING "SD data clock in Hz")
set(FV_MBEDTLS_DIR "${PICO_SDK_PATH}/lib/mbedtls" CACHE PATH "Mbed TLS source")
option(FV_HMAC_PICO "Use RP2354 SHA accelerator for HMAC" ON)
option(FV_CIPHERS_RAM "Run cipher hot paths and tables from SRAM" ON)
add_subdirectory(../../src/crypto crypto)
add_subdirectory(../../src/storage storage)
# Compile portable next-stage logic for ARM without exposing it in the benchmark.
add_subdirectory(../../src/security security)
add_subdirectory(../../src/benchmark benchmark)
add_subdirectory(../../third_party/pico_sd/src pico_sd)
add_executable(fuse_vault_v2_bench main.c usb.c worker.c
  ../../src/platform/rp2354/sd_native.c)
target_include_directories(fuse_vault_v2_bench PRIVATE .
  ../../src/platform/rp2354/include ../../src/storage/include)
target_compile_definitions(fuse_vault_v2_bench PRIVATE
  FV_SD_CLOCK_HZ=${FV_SD_CLOCK_HZ}
  PICO_CORE1_STACK_SIZE=4096 PICO_STACK_SIZE=4096)
target_compile_options(fuse_vault_v2_bench PRIVATE -Wall -Wextra -Werror -fstack-usage)
target_link_libraries(fuse_vault_v2_bench PRIVATE fv_benchmark pico_stdlib pico_multicore
  pico_unique_id tinyusb_device no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
pico_enable_stdio_usb(fuse_vault_v2_bench 0)
pico_enable_stdio_uart(fuse_vault_v2_bench 0)
pico_add_linker_script_override_path(fuse_vault_v2_bench ${CMAKE_CURRENT_LIST_DIR}/../security/linker FILES pico_flash_region.ld)
pico_add_extra_outputs(fuse_vault_v2_bench)
