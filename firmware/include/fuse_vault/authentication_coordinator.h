#ifndef FUSE_VAULT_AUTHENTICATION_COORDINATOR_H
#define FUSE_VAULT_AUTHENTICATION_COORDINATOR_H

#include "fuse_vault/app.h"
#include "fuse_vault/credential_envelope.h"
#include "fuse_vault/persistence.h"

typedef enum {
    FV_AUTHENTICATE_OK = 0,
    FV_AUTHENTICATE_REJECTED,
    FV_AUTHENTICATE_FATAL,
} fv_authenticate_result_t;

typedef struct {
    fv_volume_master_key_t vmk;
    bool vmk_valid;
} fv_authentication_session_t;

typedef struct {
    fv_secret_encoding_t encoding;
    fv_device_secret_t roots;
    fv_vault_header_t header;
    fv_volume_master_key_t candidate_vmk;
} fv_authentication_workspace_t;

/* Authentication is valid only after the state machine has durably reserved
 * an attempt. On success the VMK moves into session and remains there until
 * fv_authentication_session_clear() is called on lock, eject, or fault. */
fv_authenticate_result_t fv_authenticate(
    const fv_app_t *app, fv_platform_services_t *services,
    fv_authentication_session_t *session,
    fv_authentication_workspace_t *workspace);

void fv_authentication_session_clear(fv_authentication_session_t *session);

#endif
