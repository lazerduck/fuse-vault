#ifndef FUSE_VAULT_CREDENTIAL_CHANGE_H
#define FUSE_VAULT_CREDENTIAL_CHANGE_H

#include "fuse_vault/authentication_coordinator.h"

/* Requires the authenticated local settings flow with USB detached.
 * A false result is ambiguous: lock/fault and recover from the journal on boot. */
bool fv_credential_change(const fv_app_t *app, fv_platform_services_t *services,
    const fv_authentication_session_t *session, const fv_credential_costs_t *costs);

#endif
