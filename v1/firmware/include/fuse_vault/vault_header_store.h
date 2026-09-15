#ifndef FUSE_VAULT_VAULT_HEADER_STORE_H
#define FUSE_VAULT_VAULT_HEADER_STORE_H

#include "fuse_vault/block_device.h"
#include "fuse_vault/persistence.h"

#define FV_VAULT_HEADER_FORMAT_VERSION 2u
#define FV_VAULT_HEADER_ENCODING_VERSION 2u
#define FV_VAULT_HEADER_RECORD_SIZE 256u
#define FV_VAULT_HEADER_SLOT_COUNT 2u
#define FV_VAULT_HEADER_TAG_SIZE 32u

typedef enum {
    FV_VAULT_HEADER_STORE_OK = 0,
    FV_VAULT_HEADER_STORE_NOT_FOUND,
    FV_VAULT_HEADER_STORE_INVALID,
    FV_VAULT_HEADER_STORE_IO_ERROR,
} fv_vault_header_store_result_t;

/* Serialization is canonical little-endian and never exposes native layout. */
fv_vault_header_store_result_t fv_vault_header_serialize(
    const fv_vault_header_t *header, const fv_device_secret_t *roots,
    uint8_t output[FV_VAULT_HEADER_RECORD_SIZE]);
fv_vault_header_store_result_t fv_vault_header_parse(
    const uint8_t input[FV_VAULT_HEADER_RECORD_SIZE],
    const fv_device_secret_t *roots, const uint8_t expected_vault_id[FV_VAULT_ID_SIZE],
    fv_vault_header_t *header);

fv_vault_header_store_result_t fv_vault_header_store_load(
    fv_block_device_t *device, const fv_device_secret_t *roots,
    const uint8_t expected_vault_id[FV_VAULT_ID_SIZE], fv_vault_header_t *header);
fv_vault_header_store_result_t fv_vault_header_store_update(
    fv_block_device_t *device, const fv_device_secret_t *roots,
    const fv_vault_header_t *header);

/* The anchor uses the authenticated canonical header tag, not native struct bytes. */
bool fv_vault_header_anchor(const fv_vault_header_t *header,
    const fv_device_secret_t *roots, fv_security_state_t *state);
fv_vault_header_store_result_t fv_vault_header_store_load_committed(
    fv_block_device_t *device, const fv_device_secret_t *roots,
    const uint8_t vault_id[FV_VAULT_ID_SIZE], const fv_security_state_t *state,
    fv_vault_header_t *header);
/* Writes/verifies the inactive header. The journal must be committed separately. */
fv_vault_header_store_result_t fv_vault_header_store_stage(
    fv_block_device_t *device, const fv_device_secret_t *roots,
    const fv_security_state_t *state, const fv_vault_header_t *header);

#endif
