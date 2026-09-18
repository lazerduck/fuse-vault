#ifndef FV_PICO_CAMELLIA_RAM_H
#define FV_PICO_CAMELLIA_RAM_H

#include <stdint.h>
#include "mbedtls/build_info.h"

/* Preserve the vendor common.h/header ordering, as for AES. These declarations
 * attach placement attributes to the unchanged upstream definitions. */
struct mbedtls_camellia_context;
int mbedtls_camellia_crypt_ecb(struct mbedtls_camellia_context *ctx, int mode,
    const unsigned char input[16], unsigned char output[16])
    __attribute__((section(".time_critical.fv_camellia_ecb")));
static void camellia_feistel(const uint32_t x[2], const uint32_t k[2], uint32_t z[2])
    __attribute__((section(".time_critical.fv_camellia_feistel")));

/* Round lookup tables must also avoid flash fetches. Key-setup constants stay
 * in flash. AES already generates its lookup tables into RAM. */
static const unsigned char FSb[256]
    __attribute__((section(".time_critical.fv_camellia_sbox1")));
#if !defined(MBEDTLS_CAMELLIA_SMALL_MEMORY)
static const unsigned char FSb2[256]
    __attribute__((section(".time_critical.fv_camellia_sbox2")));
static const unsigned char FSb3[256]
    __attribute__((section(".time_critical.fv_camellia_sbox3")));
static const unsigned char FSb4[256]
    __attribute__((section(".time_critical.fv_camellia_sbox4")));
#endif

#endif
