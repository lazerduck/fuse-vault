#ifndef FV_KEY_WRAP_INTERNAL_H
#define FV_KEY_WRAP_INTERNAL_H
#include <stdint.h>
/* Generic unpadded KW, fixed 32-byte share. IDs 1 AES / 2 Camellia use
 * 256-bit KEKs; ID 3 SM4 uses the first 128 bits of the derived key slot.
 * SM4/Camellia use the KW construction, not a claim of approved AES-KW.
 * Outputs clear on error. Buffers must be disjoint. Internal, not a USB API. */
int fv_share_wrap(uint16_t id,const uint8_t key[32],const uint8_t input[32],uint8_t output[40]);
int fv_share_unwrap(uint16_t id,const uint8_t key[32],const uint8_t input[40],uint8_t output[32]);
#endif
