#ifndef FV_KEY_WRAP_INTERNAL_H
#define FV_KEY_WRAP_INTERNAL_H
#include <stdint.h>
/* Standard unpadded KW, fixed 32-byte share and 256-bit KEK; IDs 1 AES, 2 Camellia.
 * Outputs clear on error. Buffers must be disjoint. Internal, not a USB API. */
int fv_share_wrap(uint16_t id,const uint8_t key[32],const uint8_t input[32],uint8_t output[40]);
int fv_share_unwrap(uint16_t id,const uint8_t key[32],const uint8_t input[40],uint8_t output[32]);
#endif
