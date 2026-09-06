cmake_minimum_required(VERSION 3.15)

project(fuse_vault_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(FUSE_VAULT_ENABLE_SANITIZERS
    "Build host verification with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(FUSE_VAULT_ENABLE_SANITIZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Fuse Vault sanitizer verification requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(fuse_vault_ascon STATIC
    ../third_party/ascon_aead128_ref/aead.c
)
target_include_directories(fuse_vault_ascon PUBLIC
    ../third_party/ascon_aead128_ref
)

add_library(fuse_vault_core STATIC
    ../src/block_slice.c
    ../src/authentication_coordinator.c
    ../src/app.c
    ../src/boot_recovery.c
    ../src/credential_envelope.c
    ../src/crypto_stack.c
    ../src/crypto_pipeline.c
    ../src/device_roots.c
    ../src/device_services.c
    ../src/device_provisioning.c
    ../src/device_runtime.c
    ../src/display.c
    ../src/encrypted_block.c
    ../src/input.c
    ../src/input_bindings.c
    ../src/journal_authenticator.c
    ../src/media_layout.c
    ../src/provisioning_coordinator.c
    ../src/peripheral_safety.c
    ../src/secret_input.c
    ../src/security_journal.c
    ../src/ui.c
    ../src/vault_header_store.c
    ../src/virtual_msc.c
)

find_package(OpenSSL REQUIRED COMPONENTS Crypto)
target_link_libraries(fuse_vault_core PUBLIC OpenSSL::Crypto)
target_link_libraries(fuse_vault_core PUBLIC fuse_vault_ascon)

target_include_directories(fuse_vault_core PUBLIC
    ../include
)

target_compile_options(fuse_vault_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)

add_executable(fuse_vault_simulator
    simulator.c
)
target_link_libraries(fuse_vault_simulator PRIVATE fuse_vault_core)
target_compile_options(fuse_vault_simulator PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
endif()

if(GTK3_FOUND)
    add_executable(fuse_vault_display_simulator
        display_simulator.c
    )
    target_link_libraries(fuse_vault_display_simulator PRIVATE
        fuse_vault_core
        fuse_vault_host_services
        PkgConfig::GTK3
    )
    target_compile_options(fuse_vault_display_simulator PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    )
else()
    message(STATUS "GTK 3 not found: graphical display simulator disabled")
endif()

enable_testing()
if(GTK3_FOUND)
    add_executable(fuse_vault_display_simulator_tests display_simulator_tests.c)
    target_link_libraries(fuse_vault_display_simulator_tests PRIVATE
        fuse_vault_core fuse_vault_host_services PkgConfig::GTK3)
    target_compile_options(fuse_vault_display_simulator_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    add_test(NAME fuse_vault_display_simulator
        COMMAND fuse_vault_display_simulator_tests)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME fuse_vault_otp_policy_consistency
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_LIST_DIR}/../../tools/verify_otp_policy.py)
add_executable(fuse_vault_tests
    tests.c
)
target_link_libraries(fuse_vault_tests PRIVATE fuse_vault_core)
target_compile_options(fuse_vault_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_state_machine COMMAND fuse_vault_tests)

add_library(fuse_vault_host_services STATIC
    host_services.c
    file_block_device.c
)
target_link_libraries(fuse_vault_host_services PUBLIC fuse_vault_core)
target_include_directories(fuse_vault_host_services PUBLIC
    .
    ../include
)
target_compile_options(fuse_vault_host_services PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)

add_executable(fuse_vault_host_services_tests
    host_services_tests.c
)
target_link_libraries(fuse_vault_host_services_tests PRIVATE
    fuse_vault_host_services
)
target_compile_options(fuse_vault_host_services_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_host_services COMMAND fuse_vault_host_services_tests)

add_executable(fuse_vault_nor_flash_tests
    nor_flash.c
    nor_flash_tests.c
)
target_include_directories(fuse_vault_nor_flash_tests PRIVATE .)
target_compile_options(fuse_vault_nor_flash_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_nor_flash_semantics COMMAND fuse_vault_nor_flash_tests)

add_executable(fuse_vault_security_journal_tests
    ../src/security_journal.c
    nor_flash.c
    security_journal_tests.c
)
target_include_directories(fuse_vault_security_journal_tests PRIVATE
    .
    ../include
)
target_compile_options(fuse_vault_security_journal_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_security_journal COMMAND fuse_vault_security_journal_tests)

add_executable(fuse_vault_crypto_tests
    crypto_tests.c
)
target_link_libraries(fuse_vault_crypto_tests PRIVATE fuse_vault_core)
target_compile_options(fuse_vault_crypto_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_crypto_known_answers COMMAND fuse_vault_crypto_tests)

add_executable(fuse_vault_device_roots_tests
    ../src/device_roots.c
    device_roots_tests.c
)
target_include_directories(fuse_vault_device_roots_tests PRIVATE ../include)
target_compile_options(fuse_vault_device_roots_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_device_roots_lifecycle COMMAND fuse_vault_device_roots_tests)

add_library(fuse_vault_host_otp STATIC
    otp_file.c
)
target_include_directories(fuse_vault_host_otp PUBLIC . ../include)
target_compile_options(fuse_vault_host_otp PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)

add_executable(fuse_vault_otp_file_tool
    otp_file_tool.c
)
target_link_libraries(fuse_vault_otp_file_tool PRIVATE
    fuse_vault_core
    fuse_vault_host_otp
)
target_compile_options(fuse_vault_otp_file_tool PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)

add_executable(fuse_vault_otp_file_tests
    otp_file_tests.c
)
target_link_libraries(fuse_vault_otp_file_tests PRIVATE
    fuse_vault_core
    fuse_vault_host_otp
)
target_compile_options(fuse_vault_otp_file_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_otp_file_semantics COMMAND fuse_vault_otp_file_tests)

add_executable(fuse_vault_device_provisioning_tests
    device_provisioning_tests.c
)
target_link_libraries(fuse_vault_device_provisioning_tests PRIVATE
    fuse_vault_core
    fuse_vault_host_otp
)
target_compile_options(fuse_vault_device_provisioning_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_device_provisioning
    COMMAND fuse_vault_device_provisioning_tests)

add_executable(fuse_vault_credential_envelope_tests
    credential_envelope_tests.c
)
target_link_libraries(fuse_vault_credential_envelope_tests PRIVATE
    fuse_vault_core
)
target_compile_options(fuse_vault_credential_envelope_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_credential_envelope
    COMMAND fuse_vault_credential_envelope_tests)

add_executable(fuse_vault_provisioning_coordinator_tests
    provisioning_coordinator_tests.c
)
target_link_libraries(fuse_vault_provisioning_coordinator_tests PRIVATE
    fuse_vault_core
)
target_compile_options(fuse_vault_provisioning_coordinator_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_provisioning_coordinator
    COMMAND fuse_vault_provisioning_coordinator_tests)

add_executable(fuse_vault_boot_recovery_tests
    boot_recovery_tests.c
)
target_link_libraries(fuse_vault_boot_recovery_tests PRIVATE
    fuse_vault_core
    fuse_vault_host_services
)
target_compile_options(fuse_vault_boot_recovery_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_boot_recovery
    COMMAND fuse_vault_boot_recovery_tests)

add_executable(fuse_vault_authentication_coordinator_tests
    authentication_coordinator_tests.c
)
target_link_libraries(fuse_vault_authentication_coordinator_tests PRIVATE
    fuse_vault_core
)
target_compile_options(fuse_vault_authentication_coordinator_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_authentication_coordinator
    COMMAND fuse_vault_authentication_coordinator_tests)

add_executable(fuse_vault_input_tests
    input_tests.c
)
target_link_libraries(fuse_vault_input_tests PRIVATE fuse_vault_core)
target_compile_options(fuse_vault_input_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_input_controller COMMAND fuse_vault_input_tests)

add_executable(fuse_vault_vault_header_store_tests vault_header_store_tests.c)
target_link_libraries(fuse_vault_vault_header_store_tests PRIVATE fuse_vault_core)
target_compile_options(fuse_vault_vault_header_store_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
add_test(NAME fuse_vault_vault_header_store
    COMMAND fuse_vault_vault_header_store_tests)

add_executable(fuse_vault_lifecycle_msc_tests lifecycle_msc_tests.c)
target_link_libraries(fuse_vault_lifecycle_msc_tests PRIVATE
    fuse_vault_core fuse_vault_host_services)
target_compile_options(fuse_vault_lifecycle_msc_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
add_test(NAME fuse_vault_stage3_lifecycle_msc
    COMMAND fuse_vault_lifecycle_msc_tests)

add_executable(fuse_vault_persistent_vault_tests persistent_vault_tests.c)
target_link_libraries(fuse_vault_persistent_vault_tests PRIVATE
    fuse_vault_core fuse_vault_host_services)
target_compile_options(fuse_vault_persistent_vault_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
add_test(NAME fuse_vault_persistent_vault
    COMMAND fuse_vault_persistent_vault_tests)

add_executable(fuse_vault_encrypted_block_tests encrypted_block_tests.c)
target_link_libraries(fuse_vault_encrypted_block_tests PRIVATE
    fuse_vault_core fuse_vault_host_services)
target_compile_options(fuse_vault_encrypted_block_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
add_test(NAME fuse_vault_encrypted_block COMMAND fuse_vault_encrypted_block_tests)

add_executable(fuse_vault_peripheral_safety_tests peripheral_safety_tests.c)
target_link_libraries(fuse_vault_peripheral_safety_tests PRIVATE fuse_vault_core)
target_compile_options(fuse_vault_peripheral_safety_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
add_test(NAME fuse_vault_stage4_peripheral_safety
    COMMAND fuse_vault_peripheral_safety_tests)

add_executable(fuse_vault_crypto_stack_tests crypto_stack_tests.c)
target_link_libraries(fuse_vault_crypto_stack_tests PRIVATE fuse_vault_core)
target_compile_options(fuse_vault_crypto_stack_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
add_test(NAME fuse_vault_crypto_stack_registry
    COMMAND fuse_vault_crypto_stack_tests)

add_executable(fuse_vault_crypto_pipeline_tests crypto_pipeline_tests.c)
target_link_libraries(fuse_vault_crypto_pipeline_tests PRIVATE fuse_vault_core)
target_compile_options(fuse_vault_crypto_pipeline_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
add_test(NAME fuse_vault_crypto_pipeline
    COMMAND fuse_vault_crypto_pipeline_tests)

add_executable(fuse_vault_media_layout_tests media_layout_tests.c)
target_link_libraries(fuse_vault_media_layout_tests PRIVATE fuse_vault_core)
target_compile_options(fuse_vault_media_layout_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
add_test(NAME fuse_vault_media_layout
    COMMAND fuse_vault_media_layout_tests)

add_executable(fuse_vault_device_runtime_tests device_runtime_tests.c)
target_link_libraries(fuse_vault_device_runtime_tests PRIVATE
    fuse_vault_core fuse_vault_host_services)
target_compile_options(fuse_vault_device_runtime_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
add_test(NAME fuse_vault_device_runtime
    COMMAND fuse_vault_device_runtime_tests)

add_executable(fuse_vault_device_services_tests
    device_services_tests.c
    file_block_device.c
    nor_flash.c
    otp_file.c
)
target_link_libraries(fuse_vault_device_services_tests PRIVATE fuse_vault_core)
target_include_directories(fuse_vault_device_services_tests PRIVATE .)
target_compile_options(fuse_vault_device_services_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
add_test(NAME fuse_vault_device_services
    COMMAND fuse_vault_device_services_tests)

add_executable(fuse_vault_rp2354_usb_msc_tests
    ../src/rp2354_usb_msc.c
    rp2354_usb_msc_tests.c
)
target_include_directories(fuse_vault_rp2354_usb_msc_tests PRIVATE
    .
    ../include
)
target_compile_definitions(fuse_vault_rp2354_usb_msc_tests PRIVATE
    FUSE_VAULT_USB_MUX_TRUTH_TABLE_CONFIRMED=1
    FUSE_VAULT_USB_PRESENCE_POLARITY_CONFIRMED=1
)
target_compile_options(fuse_vault_rp2354_usb_msc_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
)
add_test(NAME fuse_vault_rp2354_usb_msc
    COMMAND fuse_vault_rp2354_usb_msc_tests)
