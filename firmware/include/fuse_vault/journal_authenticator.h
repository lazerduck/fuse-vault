#ifndef FUSE_VAULT_JOURNAL_AUTHENTICATOR_H
#define FUSE_VAULT_JOURNAL_AUTHENTICATOR_H

#include "fuse_vault/persistence.h"
#include "fuse_vault/security_journal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t hmac_key[FV_JOURNAL_TAG_SIZE];
    uint8_t kmac_key[FV_JOURNAL_TAG_SIZE];
    fv_journal_authenticator_t interface;
    bool initialized;
} fv_dual_journal_authenticator_t;

bool fv_kmac256(const uint8_t *key, size_t key_length,
                const uint8_t *message, size_t message_length,
                const uint8_t *customization, size_t customization_length,
                uint8_t *output, size_t output_length);

bool fv_hmac_sha256(const uint8_t *key, size_t key_length,
                    const uint8_t *first, size_t first_length,
                    const uint8_t *second, size_t second_length,
                    uint8_t output[FV_JOURNAL_TAG_SIZE]);

bool fv_dual_journal_authenticator_init(
    fv_dual_journal_authenticator_t *authenticator,
    const fv_device_secret_t *device_roots,
    const uint8_t device_id[FV_VAULT_ID_SIZE]);
void fv_dual_journal_authenticator_deinit(
    fv_dual_journal_authenticator_t *authenticator);

#endif
