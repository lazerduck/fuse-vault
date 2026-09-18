#ifndef FUSE_VAULT_CRYPTO_STACK_H
#define FUSE_VAULT_CRYPTO_STACK_H

#include "fuse_vault/persistence.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t algorithm_id;
    uint16_t algorithm_version;
    const char *name;
    bool available;
    bool duplicates_allowed;
} fv_encryption_algorithm_info_t;

size_t fv_crypto_stack_algorithm_count(void);
const fv_encryption_algorithm_info_t *fv_crypto_stack_algorithm_at(
    size_t index);
const fv_encryption_algorithm_info_t *fv_crypto_stack_algorithm_find(
    uint16_t algorithm_id, uint16_t algorithm_version);

/* Structural validation accepts reserved, unavailable algorithm IDs only when
 * require_available is false. Runtime format/unlock paths must pass true. */
bool fv_crypto_stack_descriptor_valid(
    const fv_encryption_stack_descriptor_t *descriptor,
    bool require_available);

void fv_crypto_stack_default(fv_encryption_stack_descriptor_t *descriptor);
size_t fv_crypto_stack_preset_count(void);
const char *fv_crypto_stack_preset_name(size_t preset);
bool fv_crypto_stack_preset(size_t preset,
                            fv_encryption_stack_descriptor_t *descriptor);

#endif
