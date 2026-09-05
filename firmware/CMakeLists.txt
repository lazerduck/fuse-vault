cmake_minimum_required(VERSION 3.15)

# Fuse Vault is a custom RP2354A board. Make its board definition visible to
# the SDK before importing it, since the board selects the target platform.
set(PICO_BOARD fuse_vault CACHE STRING "Target board")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards")

if(NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()

if(NOT PICO_SDK_PATH)
    message(FATAL_ERROR
        "PICO_SDK_PATH is not set. Install Pico SDK 2.3.0 or later and pass "
        "-DPICO_SDK_PATH=/path/to/pico-sdk when configuring.")
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(fuse_vault_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(PICO_SDK_VERSION_STRING VERSION_LESS "2.3.0")
    message(FATAL_ERROR "Fuse Vault requires Pico SDK 2.3.0 or later")
endif()

pico_sdk_init()

add_executable(fuse_vault
    src/app.c
    src/main.c
    src/rp2354_random.c
    src/secret_input.c
    src/security_journal.c
    src/ui.c
)

target_include_directories(fuse_vault PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/include"
)

target_link_libraries(fuse_vault PRIVATE
    pico_stdlib
    pico_rand
)

# Apply strict diagnostics to our code without imposing them on Pico SDK source
# files, some of which intentionally use supported GNU C extensions.
set_source_files_properties(src/app.c src/main.c src/rp2354_random.c src/secret_input.c src/security_journal.c src/ui.c PROPERTIES COMPILE_OPTIONS
    "-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow"
)

# The production device must not expose an interface before the user has
# selected a mode and authenticated. UART remains available for early board
# bring-up once its pins have been confirmed.
pico_enable_stdio_usb(fuse_vault 0)
pico_enable_stdio_uart(fuse_vault 0)

pico_set_program_name(fuse_vault "Fuse Vault")
pico_set_program_description(fuse_vault "Fuse Vault RP2354A firmware")
pico_set_program_version(fuse_vault "0.1.0-dev")

pico_add_extra_outputs(fuse_vault)
