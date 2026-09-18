#ifndef FV_PICO_AES_RAM_H
#define FV_PICO_AES_RAM_H

/* Do not include aes.h ahead of the library's common.h: it establishes
 * Mbed TLS's internal private-member access macros. */
struct mbedtls_aes_context;

/* The Pico linker/startup copies .time_critical
 * sections to SRAM. The upstream definitions inherit these attributes. */
int mbedtls_internal_aes_encrypt(struct mbedtls_aes_context *ctx,
    const unsigned char input[16], unsigned char output[16])
    __attribute__((section(".time_critical.fv_aes_encrypt")));
int mbedtls_internal_aes_decrypt(struct mbedtls_aes_context *ctx,
    const unsigned char input[16], unsigned char output[16])
    __attribute__((section(".time_critical.fv_aes_decrypt")));

#endif
