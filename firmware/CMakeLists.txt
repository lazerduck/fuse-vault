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

option(FUSE_VAULT_RELEASE_BUILD
    "Build a production candidate with all evidence gates enforced" OFF)
option(FUSE_VAULT_ENABLE_DISPLAY
    "Enable the ST7735S display" ON)
option(FUSE_VAULT_COMPILE_UNVALIDATED_USB_MSC
    "Compile-check the USB MSC attach path without enabling connector routing" OFF)
set(FUSE_VAULT_RELEASE_CONFIRMATION "" CACHE STRING
    "Required acknowledgement for a production-candidate build")
set(FUSE_VAULT_RELEASE_VERSION "" CACHE STRING
    "Production semantic version, for example 1.0.0")
set(FUSE_VAULT_USB_VID "" CACHE STRING
    "Assigned production USB VID as a C hexadecimal literal")
set(FUSE_VAULT_USB_PID "" CACHE STRING
    "Assigned production USB PID as a C hexadecimal literal")
set(FUSE_VAULT_BENCH_USB_A_PRESENT_LEVEL "" CACHE STRING
    "Bench-only USB-A presence active level (0 or 1; empty keeps it gated)")
set(FUSE_VAULT_BENCH_USB_C_PRESENT_LEVEL "" CACHE STRING
    "Bench-only USB-C presence active level (0 or 1; empty keeps it gated)")
set(FUSE_VAULT_BENCH_SD_CARD_DETECT_LEVEL "" CACHE STRING
    "Bench-only SD card-detect active level (0 or 1; empty keeps it gated)")

if(FUSE_VAULT_RELEASE_BUILD)
    if(NOT FUSE_VAULT_ENABLE_DISPLAY)
        message(FATAL_ERROR "A release requires the display enabled")
    endif()
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(FATAL_ERROR
            "FUSE_VAULT_RELEASE_BUILD requires CMAKE_BUILD_TYPE=Release")
    endif()
    if(NOT FUSE_VAULT_RELEASE_CONFIRMATION STREQUAL
       "I_HAVE_REVIEWED_THE_RELEASE_EVIDENCE")
        message(FATAL_ERROR
            "Set FUSE_VAULT_RELEASE_CONFIRMATION="
            "I_HAVE_REVIEWED_THE_RELEASE_EVIDENCE only after the hardware "
            "profile and release evidence have been reviewed")
    endif()
    if(NOT FUSE_VAULT_RELEASE_VERSION MATCHES
       "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR
            "FUSE_VAULT_RELEASE_VERSION must be a three-part semantic version")
    endif()
    if(NOT FUSE_VAULT_USB_VID MATCHES
       "^0[xX][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]$" OR
       NOT FUSE_VAULT_USB_PID MATCHES
       "^0[xX][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]$")
        message(FATAL_ERROR
            "A release requires assigned four-digit hexadecimal USB VID/PID values")
    endif()
    string(TOLOWER "${FUSE_VAULT_USB_VID}" _fuse_vault_vid_lower)
    string(TOLOWER "${FUSE_VAULT_USB_PID}" _fuse_vault_pid_lower)
    if(_fuse_vault_vid_lower STREQUAL "0xcafe" OR
       _fuse_vault_pid_lower STREQUAL "0x4011")
        message(FATAL_ERROR
            "TinyUSB development VID/PID values are forbidden in a release")
    endif()
    if(FUSE_VAULT_COMPILE_UNVALIDATED_USB_MSC OR
       NOT FUSE_VAULT_BENCH_USB_A_PRESENT_LEVEL STREQUAL "" OR
       NOT FUSE_VAULT_BENCH_USB_C_PRESENT_LEVEL STREQUAL "" OR
       NOT FUSE_VAULT_BENCH_SD_CARD_DETECT_LEVEL STREQUAL "")
        message(FATAL_ERROR
            "Bench-only hardware overrides are forbidden in a release; record "
            "validated values in the reviewed board definition")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_LIST_DIR}/../tools/verify_otp_policy.py --release
        RESULT_VARIABLE _fuse_vault_policy_result
        OUTPUT_VARIABLE _fuse_vault_policy_output
        ERROR_VARIABLE _fuse_vault_policy_error)
    if(NOT _fuse_vault_policy_result EQUAL 0)
        message(FATAL_ERROR
            "OTP/security release policy is not satisfied:\n"
            "${_fuse_vault_policy_output}${_fuse_vault_policy_error}")
    endif()
endif()

if(PICO_SDK_VERSION_STRING VERSION_LESS "2.3.0")
    message(FATAL_ERROR "Fuse Vault requires Pico SDK 2.3.0 or later")
endif()

set(PICO_MBEDTLS_CONFIG_FILE
    "${CMAKE_CURRENT_LIST_DIR}/mbedtls_config.h")
pico_sdk_init()

add_executable(fuse_vault
    src/block_slice.c
    src/authentication_coordinator.c
    src/app.c
    src/boot_recovery.c
    src/credential_envelope.c
    src/crypto_stack.c
    src/crypto_pipeline.c
    src/device_roots.c
    src/device_services.c
    src/device_provisioning.c
    src/device_runtime.c
    src/display.c
    src/encrypted_block.c
    src/input.c
    src/input_bindings.c
    src/journal_authenticator.c
    src/media_layout.c
    src/provisioning_coordinator.c
    src/peripheral_safety.c
    src/main.c
    src/rp2354_random.c
    src/rp2354_connector.c
    src/rp2354_display.c
    src/rp2354_sd.c
    src/rp2354_input.c
    src/rp2354_otp.c
    src/rp2354_security_flash.c
    src/rp2354_usb_descriptors.c
    src/rp2354_usb_msc.c
    src/secret_input.c
    src/security_journal.c
    src/ui.c
    src/vault_header_store.c
    src/virtual_msc.c
    third_party/ascon_aead128_ref/aead.c
)

if(FUSE_VAULT_RELEASE_BUILD)
    target_compile_definitions(fuse_vault PRIVATE
        FUSE_VAULT_RELEASE_BUILD=1
        FUSE_VAULT_USB_VID=${FUSE_VAULT_USB_VID}
        FUSE_VAULT_USB_PID=${FUSE_VAULT_USB_PID})
endif()

target_compile_definitions(fuse_vault PRIVATE
    FUSE_VAULT_ENABLE_DISPLAY=$<BOOL:${FUSE_VAULT_ENABLE_DISPLAY}>)

if(FUSE_VAULT_COMPILE_UNVALIDATED_USB_MSC)
    message(WARNING
        "Compile-checking the USB MSC attach path. Connector routing remains "
        "disabled at runtime until its electrical truth table is confirmed.")
    target_compile_definitions(fuse_vault PRIVATE
        FUSE_VAULT_COMPILE_UNVALIDATED_USB_MSC=1)
endif()

if(NOT FUSE_VAULT_BENCH_USB_A_PRESENT_LEVEL STREQUAL "" OR
   NOT FUSE_VAULT_BENCH_USB_C_PRESENT_LEVEL STREQUAL "")
    if(NOT FUSE_VAULT_BENCH_USB_A_PRESENT_LEVEL MATCHES "^[01]$" OR
       NOT FUSE_VAULT_BENCH_USB_C_PRESENT_LEVEL MATCHES "^[01]$")
        message(FATAL_ERROR
            "Both USB presence levels must be supplied as 0 or 1")
    endif()
    message(WARNING
        "Enabling candidate USB presence levels for disposable-board bring-up. "
        "Verify both levels electrically before a production build.")
    target_compile_definitions(fuse_vault PRIVATE
        FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED=1
        FUSE_VAULT_USB_A_PRESENT_LEVEL=${FUSE_VAULT_BENCH_USB_A_PRESENT_LEVEL}
        FUSE_VAULT_USB_C_PRESENT_LEVEL=${FUSE_VAULT_BENCH_USB_C_PRESENT_LEVEL})
endif()

if(NOT FUSE_VAULT_BENCH_SD_CARD_DETECT_LEVEL STREQUAL "")
    if(NOT FUSE_VAULT_BENCH_SD_CARD_DETECT_LEVEL MATCHES "^[01]$")
        message(FATAL_ERROR "SD card-detect level must be 0 or 1")
    endif()
    message(WARNING
        "Enabling a candidate SD card-detect level for disposable-board "
        "bring-up. Verify it electrically before a production build.")
    target_compile_definitions(fuse_vault PRIVATE
        FUSE_VAULT_SD_CARD_DETECT_POLARITY_CONFIRMED=1
        FUSE_VAULT_SD_CARD_DETECT_ACTIVE_LEVEL=${FUSE_VAULT_BENCH_SD_CARD_DETECT_LEVEL})
endif()

target_include_directories(fuse_vault PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
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
    hardware_spi
    pico_sha256
    pico_mbedtls_crypto
    pico_mbedtls_headers
    tinyusb_device
)

# Apply strict diagnostics to our code without imposing them on Pico SDK source
# files, some of which intentionally use supported GNU C extensions.
set_source_files_properties(src/app.c src/authentication_coordinator.c src/block_slice.c src/boot_recovery.c src/credential_envelope.c src/crypto_stack.c src/crypto_pipeline.c src/device_provisioning.c src/device_runtime.c src/device_roots.c src/device_services.c src/display.c src/encrypted_block.c src/input.c src/input_bindings.c src/journal_authenticator.c src/main.c src/media_layout.c src/peripheral_safety.c src/provisioning_coordinator.c src/rp2354_connector.c src/rp2354_display.c src/rp2354_input.c src/rp2354_otp.c src/rp2354_random.c src/rp2354_sd.c src/rp2354_security_flash.c src/rp2354_usb_descriptors.c src/rp2354_usb_msc.c src/secret_input.c src/security_journal.c src/ui.c src/vault_header_store.c src/virtual_msc.c PROPERTIES COMPILE_OPTIONS
    "-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow"
)

# The production device must not expose an interface before the user has
# selected a mode and authenticated. UART remains available for early board
# bring-up once its pins have been confirmed.
pico_enable_stdio_usb(fuse_vault 0)
pico_enable_stdio_uart(fuse_vault 0)

pico_set_program_name(fuse_vault "Fuse Vault")
pico_set_program_description(fuse_vault "Fuse Vault RP2354A firmware")
if(FUSE_VAULT_RELEASE_BUILD)
    pico_set_program_version(fuse_vault "${FUSE_VAULT_RELEASE_VERSION}")
else()
    pico_set_program_version(fuse_vault "0.1.0-dev")
endif()

pico_add_extra_outputs(fuse_vault)

set_source_files_properties(src/authentication_coordinator.c PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow")
