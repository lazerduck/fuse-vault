#ifndef FUSE_VAULT_PERSISTENCE_H
#define FUSE_VAULT_PERSISTENCE_H

#include "fuse_vault/entry_method.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FV_DEVICE_ROOT_SIZE 32u
#define FV_DEVICE_ROOT_COUNT 2u
#define FV_DEVICE_SECRET_SIZE (FV_DEVICE_ROOT_SIZE * FV_DEVICE_ROOT_COUNT)
#define FV_VAULT_ID_SIZE 16u
#define FV_SALT_SIZE 16u
#define FV_WRAPPED_VMK_CAPACITY 128u
#define FV_ENCRYPTION_STACK_FORMAT_VERSION 1u
#define FV_ENCRYPTION_STACK_MAX_LAYERS 4u

typedef enum {
    FV_PERSIST_OK = 0,
    FV_PERSIST_NOT_FOUND,
    FV_PERSIST_INVALID,
    FV_PERSIST_IO_ERROR,
} fv_persist_result_t;

typedef enum {
    FV_CRYPTO_PROFILE_UNAVAILABLE = 0,
    FV_CRYPTO_PROFILE_DUAL_FAMILY_V1 = 1,
} fv_crypto_profile_t;

/* Permanent on-media identifiers. Values are never reused for a different
 * algorithm. An ID may exist before an implementation is release-enabled. */
typedef enum {
    FV_ENCRYPTION_ALGORITHM_UNAVAILABLE = 0,
    FV_ENCRYPTION_ALGORITHM_ASCON_AEAD128 = 1,
    FV_ENCRYPTION_ALGORITHM_AES_256_XTS = 2,
    FV_ENCRYPTION_ALGORITHM_CHACHA20 = 3,
    FV_ENCRYPTION_ALGORITHM_SM4_XTS = 4,
} fv_encryption_algorithm_t;

typedef struct {
    uint16_t algorithm_id;
    uint16_t algorithm_version;
} fv_encryption_layer_descriptor_t;

typedef struct {
    uint16_t format_version;
    uint8_t layer_count;
    uint8_t reserved;
    fv_encryption_layer_descriptor_t layers[FV_ENCRYPTION_STACK_MAX_LAYERS];
} fv_encryption_stack_descriptor_t;

typedef enum {
    FV_DEVICE_SECRET_EMPTY = 0,
    FV_DEVICE_SECRET_ACTIVE,
    FV_DEVICE_SECRET_REVOKED,
    FV_DEVICE_SECRET_INVALID,
} fv_device_secret_status_t;

typedef struct {
    uint8_t device_secret[FV_DEVICE_SECRET_SIZE];
} fv_device_secret_t;

typedef struct {
    uint64_t sequence;
    uint8_t failed_attempts;
    bool provisioned;
    bool fido_initialized;
    uint8_t fido_digest[32];
    /* Zero sequence denotes a legacy vault without a committed header anchor. */
    uint64_t header_sequence;
    uint8_t header_tag[32];
} fv_security_state_t;

typedef struct {
    uint64_t sequence;
    fv_crypto_profile_t crypto_profile;
    fv_secret_method_t entry_method;
    uint8_t vault_id[FV_VAULT_ID_SIZE];
    uint8_t branch_a_salt[FV_SALT_SIZE];
    uint8_t branch_b_salt[FV_SALT_SIZE];
    uint32_t branch_a_cost;
    uint32_t branch_b_cost;
    uint16_t wrapped_vmk_length;
    uint8_t wrapped_vmk[FV_WRAPPED_VMK_CAPACITY];
    fv_encryption_stack_descriptor_t encryption_stack;
} fv_vault_header_t;

typedef struct fv_platform_services fv_platform_services_t;

typedef struct {
    bool (*random_fill)(fv_platform_services_t *services,
                        uint8_t *output, size_t length);
    fv_persist_result_t (*device_secret_status)(
        fv_platform_services_t *services, fv_device_secret_status_t *status);
    fv_persist_result_t (*provision_device_secret)(
        fv_platform_services_t *services, const fv_device_secret_t *secret);
    fv_persist_result_t (*read_device_secret)(
        fv_platform_services_t *services, fv_device_secret_t *secret);
    fv_persist_result_t (*revoke_device_secret)(
        fv_platform_services_t *services);
    fv_persist_result_t (*load_security_state)(
        fv_platform_services_t *services, fv_security_state_t *state);
    fv_persist_result_t (*store_security_state)(
        fv_platform_services_t *services, const fv_security_state_t *state);
    fv_persist_result_t (*load_vault_header)(fv_platform_services_t *services,
                                             fv_vault_header_t *header);
    /* With header_sequence anchored, stages/verifies the inactive copy only.
     * load_vault_header continues returning the journal-committed copy until
     * store_security_state publishes the replacement sequence and tag. */
    fv_persist_result_t (*store_vault_header)(fv_platform_services_t *services,
                                              const fv_vault_header_t *header);
} fv_platform_service_ops_t;

struct fv_platform_services {
    const fv_platform_service_ops_t *ops;
    void *context;
};

#endif
