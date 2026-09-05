cmake_minimum_required(VERSION 3.15)

project(fuse_vault_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(fuse_vault_ascon STATIC
    ../third_party/ascon_aead128_ref/aead.c
)
target_include_directories(fuse_vault_ascon PUBLIC
    ../third_party/ascon_aead128_ref
)

add_library(fuse_vault_core STATIC
    ../src/app.c
    ../src/credential_envelope.c
    ../src/device_roots.c
    ../src/device_provisioning.c
    ../src/journal_authenticator.c
    ../src/secret_input.c
    ../src/security_journal.c
    ../src/ui.c
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
)
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
    nor_flash.c
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
