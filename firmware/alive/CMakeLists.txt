cmake_minimum_required(VERSION 3.20)
set(PICO_BOARD fuse_vault CACHE STRING "Board")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../src/platform/rp2354/boards")
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(fuse_vault_alive C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()
if(NOT PICO_BOARD STREQUAL "fuse_vault" AND NOT PICO_BOARD STREQUAL "pico2")
  message(FATAL_ERROR "ALIVE supports only fuse_vault and pico2")
endif()

add_executable(fuse_vault_alive main.c)
target_link_libraries(fuse_vault_alive PRIVATE pico_stdlib)
target_compile_options(fuse_vault_alive PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(fuse_vault_alive PRIVATE
  USBD_PRODUCT="Fuse Vault ALIVE"
  PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0
  PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=0)
pico_enable_stdio_usb(fuse_vault_alive 1)
pico_enable_stdio_uart(fuse_vault_alive 0)
# Retain the existing journal reservation; otherwise use the SDK flash layout.
if(PICO_BOARD STREQUAL "fuse_vault")
  pico_add_linker_script_override_path(fuse_vault_alive ${CMAKE_CURRENT_LIST_DIR}/../security/linker FILES pico_flash_region.ld)
endif()
pico_add_extra_outputs(fuse_vault_alive)
