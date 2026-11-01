#ifndef FV_SESSION_ACCESS_H
#define FV_SESSION_ACCESS_H
#include "fuse_vault/access_mode.h"
#include "fuse_vault/vault.h"

/* Owned by the vault worker. Transport/UI receive snapshots, never writable
 * references. Generation binds queued commands to one authorization lifetime. */
typedef struct {
    fv_access_mode mode;
    uint32_t generation;
} fv_session_access;

fv_access_mode fv_session_mode(const fv_session_access *, const fv_vault *);
void fv_session_lock(fv_session_access *, fv_vault *);
/* Unlock grants exactly the requested scope; changing scope requires a lock. */
fv_vault_result fv_session_unlock(fv_session_access *, fv_vault *, const fv_vault_platform *,
                                  const uint8_t *, size_t, fv_access_mode);
/* Verification never changes scope or generation on success. */
fv_vault_result fv_session_reverify_fido(fv_session_access *, fv_vault *, const uint8_t *, size_t);
#endif
