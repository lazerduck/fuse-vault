#ifndef FUSE_VAULT_SECRET_INPUT_H
#define FUSE_VAULT_SECRET_INPUT_H

#include "fuse_vault/app.h"

#include <stdbool.h>
#include <stdint.h>

#define FV_SECRET_ENCODING_SIZE 8u
#define FV_UNLOCK_KEY_SIZE 32u

typedef enum {
    FV_SECRET_METHOD_WHEELS_V1 = 1,
} fv_secret_method_t;

typedef struct {
    uint8_t bytes[FV_SECRET_ENCODING_SIZE];
} fv_secret_encoding_t;

typedef struct {
    uint8_t bytes[FV_UNLOCK_KEY_SIZE];
} fv_unlock_key_t;

/*
 * Produces an unambiguous, versioned representation of the selected input.
 * This is KDF input, not an encryption key. A cryptographic backend will
 * derive fv_unlock_key_t using this encoding, a vault salt, and its parameters.
 */
bool fv_secret_input_encode(const fv_app_t *app, fv_secret_encoding_t *encoding);
bool fv_setup_secret_encode(const fv_app_t *app, fv_secret_encoding_t *encoding);

#endif
