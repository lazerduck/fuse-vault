#ifndef FUSE_VAULT_PROVISIONING_COORDINATOR_H
#define FUSE_VAULT_PROVISIONING_COORDINATOR_H

#include "fuse_vault/app.h"
#include "fuse_vault/credential_envelope.h"
#include "fuse_vault/persistence.h"

typedef enum {
    FV_SETUP_PROVISION_OK = 0,
    FV_SETUP_PROVISION_INVALID_ARGUMENT,
    FV_SETUP_PROVISION_NOT_PRISTINE,
    FV_SETUP_PROVISION_ENCODING_FAILED,
    FV_SETUP_PROVISION_RANDOM_FAILED,
    FV_SETUP_PROVISION_ROOTS_FAILED,
    FV_SETUP_PROVISION_ENVELOPE_FAILED,
    FV_SETUP_PROVISION_HEADER_FAILED,
    FV_SETUP_PROVISION_STATE_FAILED,
    FV_SETUP_PROVISION_VERIFICATION_FAILED,
} fv_setup_provision_result_t;

/*
 * Caller-owned workspace makes the lifetime of every sensitive intermediate
 * explicit. fv_setup_provision() clears the whole object before returning on
 * both success and failure.
 */
typedef struct {
    fv_secret_encoding_t encoding;
    fv_device_secret_t generated_roots;
    fv_device_secret_t stored_roots;
    fv_volume_master_key_t vmk;
    fv_vault_header_t header;
    fv_vault_header_t verified_header;
    fv_security_state_t state;
    fv_security_state_t verified_state;
} fv_setup_provision_workspace_t;

fv_setup_provision_result_t fv_setup_provision(
    const fv_app_t *app, fv_platform_services_t *services,
    const fv_credential_costs_t *costs,
    fv_setup_provision_workspace_t *workspace);

#endif
