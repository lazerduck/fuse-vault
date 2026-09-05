#ifndef FUSE_VAULT_PERSISTENCE_H
#define FUSE_VAULT_PERSISTENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FV_DEVICE_ROOT_SIZE 32u
#define FV_DEVICE_ROOT_COUNT 2u
#define FV_DEVICE_SECRET_SIZE (FV_DEVICE_ROOT_SIZE * FV_DEVICE_ROOT_COUNT)
#define FV_VAULT_ID_SIZE 16u
#define FV_SALT_SIZE 16u
#define FV_WRAPPED_VMK_CAPACITY 128u

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
} fv_security_state_t;

typedef struct {
    uint64_t sequence;
    fv_crypto_profile_t crypto_profile;
    uint32_t entry_method;
    uint8_t vault_id[FV_VAULT_ID_SIZE];
    uint8_t branch_a_salt[FV_SALT_SIZE];
    uint8_t branch_b_salt[FV_SALT_SIZE];
    uint32_t branch_a_cost;
    uint32_t branch_b_cost;
    uint16_t wrapped_vmk_length;
    uint8_t wrapped_vmk[FV_WRAPPED_VMK_CAPACITY];
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
    fv_persist_result_t (*store_vault_header)(fv_platform_services_t *services,
                                              const fv_vault_header_t *header);
} fv_platform_service_ops_t;

struct fv_platform_services {
    const fv_platform_service_ops_t *ops;
    void *context;
};

#endif
