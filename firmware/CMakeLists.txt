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

option(FUSE_VAULT_ENABLE_OTP_PROVISIONING
    "Compile irreversible device-root OTP programming support" OFF)
set(FUSE_VAULT_OTP_PROVISIONING_CONFIRMATION "" CACHE STRING
    "Required confirmation token for an OTP provisioning build")

if(FUSE_VAULT_ENABLE_OTP_PROVISIONING AND
   NOT FUSE_VAULT_OTP_PROVISIONING_CONFIRMATION STREQUAL
       "I_UNDERSTAND_OTP_WRITES_ARE_PERMANENT")
    message(FATAL_ERROR
        "OTP provisioning is irreversible. Set "
        "FUSE_VAULT_OTP_PROVISIONING_CONFIRMATION="
        "I_UNDERSTAND_OTP_WRITES_ARE_PERMANENT to build it deliberately.")
endif()

if(PICO_SDK_VERSION_STRING VERSION_LESS "2.3.0")
    message(FATAL_ERROR "Fuse Vault requires Pico SDK 2.3.0 or later")
endif()

set(PICO_MBEDTLS_CONFIG_FILE
    "${CMAKE_CURRENT_LIST_DIR}/mbedtls_config.h")
pico_sdk_init()

add_executable(fuse_vault
    src/authentication_coordinator.c
    src/app.c
    src/boot_recovery.c
    src/credential_envelope.c
    src/device_roots.c
    src/device_provisioning.c
    src/input.c
    src/input_bindings.c
    src/journal_authenticator.c
    src/provisioning_coordinator.c
    src/main.c
    src/rp2354_random.c
    src/rp2354_input.c
    src/rp2354_otp.c
    src/rp2354_security_flash.c
    src/secret_input.c
    src/security_journal.c
    src/ui.c
    third_party/ascon_aead128_ref/aead.c
)

if(FUSE_VAULT_ENABLE_OTP_PROVISIONING)
    target_compile_definitions(fuse_vault PRIVATE
        FUSE_VAULT_ENABLE_OTP_PROVISIONING=1)
endif()

target_include_directories(fuse_vault PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/third_party/ascon_aead128_ref"
)

# Keep the final 8 KiB outside the firmware image as a second, link-time guard
# in addition to the backend's runtime overlap check.
target_link_options(fuse_vault PRIVATE
    "LINKER:-T,${CMAKE_CURRENT_LIST_DIR}/linker/security_journal_assert.ld"
)

target_link_libraries(fuse_vault PRIVATE
    pico_stdlib
    pico_rand
    pico_unique_id
    pico_flash
    hardware_flash
    hardware_gpio
    pico_sha256
    pico_mbedtls_crypto
    pico_mbedtls_headers
)

# Apply strict diagnostics to our code without imposing them on Pico SDK source
# files, some of which intentionally use supported GNU C extensions.
set_source_files_properties(src/app.c src/boot_recovery.c src/credential_envelope.c src/device_provisioning.c src/device_roots.c src/input.c src/input_bindings.c src/journal_authenticator.c src/main.c src/provisioning_coordinator.c src/rp2354_input.c src/rp2354_otp.c src/rp2354_random.c src/rp2354_security_flash.c src/secret_input.c src/security_journal.c src/ui.c PROPERTIES COMPILE_OPTIONS
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

set_source_files_properties(src/authentication_coordinator.c PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow")
