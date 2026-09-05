#ifndef FUSE_VAULT_CREDENTIAL_ENVELOPE_H
#define FUSE_VAULT_CREDENTIAL_ENVELOPE_H

#include "fuse_vault/persistence.h"
#include "fuse_vault/secret_input.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FV_VMK_SIZE 32u
#define FV_CREDENTIAL_ENVELOPE_SIZE 92u
#define FV_CREDENTIAL_PBKDF2_MAX_ITERATIONS 1000000u
#define FV_CREDENTIAL_KMAC_MAX_ITERATIONS 1000000u

typedef bool (*fv_credential_random_fill_fn)(void *context, uint8_t *output,
                                              size_t length);

typedef enum {
    FV_CREDENTIAL_OK = 0,
    FV_CREDENTIAL_INVALID_ARGUMENT,
    FV_CREDENTIAL_RANDOM_FAILED,
    FV_CREDENTIAL_DERIVATION_FAILED,
    FV_CREDENTIAL_AUTHENTICATION_FAILED,
} fv_credential_result_t;

typedef struct {
    uint32_t pbkdf2_iterations;
    uint32_t kmac_iterations;
} fv_credential_costs_t;

typedef struct {
    uint8_t bytes[FV_VMK_SIZE];
} fv_volume_master_key_t;

/* Validates the complete persisted header structure, without authenticating a
 * user credential or opening the wrapped VMK. */
bool fv_vault_header_valid(const fv_vault_header_t *header);

/* header.sequence, entry_method, and vault_id must be set by the caller. */
fv_credential_result_t fv_credential_envelope_create(
    const fv_secret_encoding_t *entry,
    const fv_device_secret_t *device_roots,
    const fv_credential_costs_t *costs,
    fv_credential_random_fill_fn random_fill, void *random_context,
    fv_vault_header_t *header, fv_volume_master_key_t *vmk);

fv_credential_result_t fv_credential_envelope_open(
    const fv_secret_encoding_t *entry,
    const fv_device_secret_t *device_roots,
    const fv_vault_header_t *header, fv_volume_master_key_t *vmk);

void fv_volume_master_key_clear(fv_volume_master_key_t *vmk);

#endif
