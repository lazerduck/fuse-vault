#include "fuse_vault/rp2354_random.h"

#include "pico/rand.h"

#include <string.h>

bool fv_rp2354_random_fill(uint8_t *output, size_t length) {
    if (output == NULL && length != 0u) return false;

    while (length > 0u) {
        uint64_t random = get_rand_64();
        const size_t chunk = length < sizeof(random) ? length : sizeof(random);
        memcpy(output, &random, chunk);
        output += chunk;
        length -= chunk;
        volatile uint64_t *clear = &random;
        *clear = 0u;
    }
    return true;
}
