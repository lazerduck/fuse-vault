set(FV_MBEDTLS_DIR "" CACHE PATH "Mbed TLS 3.6 source directory (e.g. Pico SDK lib/mbedtls)")
if(NOT EXISTS "${FV_MBEDTLS_DIR}/library/aes.c")
  message(FATAL_ERROR "Set FV_MBEDTLS_DIR to an existing Mbed TLS 3.6 source checkout")
endif()
add_library(fv_crypto STATIC hmac.c hmac_self_test.c cipher.c pipeline.c backends/mbedtls.c
  ${FV_MBEDTLS_DIR}/library/aes.c
  ${FV_MBEDTLS_DIR}/library/ctr_drbg.c
  ${FV_MBEDTLS_DIR}/library/sha256.c
  ${FV_MBEDTLS_DIR}/library/camellia.c
  ${FV_MBEDTLS_DIR}/library/platform_util.c)
target_include_directories(fv_crypto PUBLIC include ${FV_MBEDTLS_DIR}/include PRIVATE .)
target_compile_definitions(fv_crypto PUBLIC MBEDTLS_CONFIG_FILE="fuse_vault/mbedtls_config.h")
target_compile_options(fv_crypto PRIVATE -Wall -Wextra -Wpedantic)

option(FV_AES_RAM "Diagnostic: run AES block routines from Pico SRAM" OFF)
option(FV_CIPHERS_RAM "Run AES and Camellia hot paths and tables from Pico SRAM" OFF)
if(FV_AES_RAM OR FV_CIPHERS_RAM)
  if(NOT TARGET pico_stdlib)
    message(FATAL_ERROR "RAM cipher placement requires a Pico SDK build")
  endif()
  # Apply only to the upstream AES translation unit; do not edit vendor code.
  set_property(SOURCE ${FV_MBEDTLS_DIR}/library/aes.c APPEND PROPERTY
    COMPILE_OPTIONS -include ${CMAKE_CURRENT_SOURCE_DIR}/backends/pico_aes_ram.h)
endif()
if(FV_CIPHERS_RAM)
  set_property(SOURCE ${FV_MBEDTLS_DIR}/library/camellia.c APPEND PROPERTY
    COMPILE_OPTIONS -include ${CMAKE_CURRENT_SOURCE_DIR}/backends/pico_camellia_ram.h)
endif()

option(FV_HMAC_PICO "Use RP2354 SHA accelerator for HMAC" OFF)
if(FV_HMAC_PICO)
  if(NOT TARGET pico_sha256)
    message(FATAL_ERROR "FV_HMAC_PICO requires a Pico SDK RP2350 build")
  endif()
  target_sources(fv_crypto PRIVATE backends/pico_hmac.c)
  target_compile_definitions(fv_crypto PRIVATE FV_HMAC_PICO=1)
  target_link_libraries(fv_crypto PRIVATE pico_sha256)
endif()
