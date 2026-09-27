#ifndef FV_PICO_SM4_RAM_H
#define FV_PICO_SM4_RAM_H
#include <gmssl/sm4.h>
/* GmSSL v3.1.1 portable block path and its lookup table. */
void sm4_encrypt(const SM4_KEY *, const uint8_t[16], uint8_t[16])
    __attribute__((section(".time_critical.fv_sm4_encrypt")));
extern const uint32_t SM4_T[256]
    __attribute__((section(".time_critical.fv_sm4_table")));
#endif
