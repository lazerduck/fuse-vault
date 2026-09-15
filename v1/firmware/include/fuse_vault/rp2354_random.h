#ifndef FUSE_VAULT_RP2354_RANDOM_H
#define FUSE_VAULT_RP2354_RANDOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool fv_rp2354_random_fill(uint8_t *output, size_t length);

#endif
