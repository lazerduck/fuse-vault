cmake_minimum_required(VERSION 3.15)
set(PICO_BOARD fuse_vault CACHE STRING "Bench board")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/../boards")
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(fuse_vault_bench C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(PICO_MBEDTLS_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/../mbedtls_config.h")
pico_sdk_init()
add_subdirectory(../third_party/pico_sd/src pico_sd)
add_executable(fuse_vault_bench
    main.c
    ../src/rp2354_sd_native.c
    ../src/rp2354_connector.c
    ../src/peripheral_safety.c
    ../src/rp2354_usb_descriptors.c
    ../src/rp2354_usb_msc.c
    ../src/diagnostics.c
    ../src/storage_profile.c
    ../src/crypto_pipeline.c
    ../src/crypto_stack.c
    ../src/journal_authenticator.c
    ../third_party/ascon_aead128_ref/aead.c)
target_include_directories(fuse_vault_bench PRIVATE .. ../include ../third_party/ascon_aead128_ref)
target_compile_definitions(fuse_vault_bench PRIVATE
    FUSE_VAULT_HEADLESS_DEBUG=1 FUSE_VAULT_ENABLE_FIDO2=0
    FUSE_VAULT_ENABLE_DISPLAY=0 FUSE_VAULT_SD_NATIVE=1
    FUSE_VAULT_USB_PID=0x4014 PICO_STACK_SIZE=65536 PICO_USE_STACK_GUARDS=1)
target_compile_definitions(fuse_vault_bench PRIVATE FUSE_VAULT_USB_PRODUCT="Fuse Vault BENCH RAM")
target_link_libraries(fuse_vault_bench PRIVATE pico_stdlib pico_unique_id
    pico_sha256 pico_mbedtls_crypto pico_mbedtls_headers tinyusb_device
    hardware_gpio hardware_exception no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
set_source_files_properties(main.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
pico_enable_stdio_usb(fuse_vault_bench 0)
pico_enable_stdio_uart(fuse_vault_bench 0)
pico_set_program_name(fuse_vault_bench "Fuse Vault disposable-card benchmark")
pico_add_linker_script_override_path(fuse_vault_bench ${CMAKE_CURRENT_LIST_DIR}/../linker/runtime
    FILES sections_stack.incl section_end.incl)
pico_set_linker_script_var(fuse_vault_bench HEAP_LIMIT "ORIGIN(RAM) + LENGTH(RAM) - 65536")
target_compile_options(fuse_vault_bench PRIVATE -fstack-usage)
pico_add_extra_outputs(fuse_vault_bench)
