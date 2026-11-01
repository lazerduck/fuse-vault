#include "fuse_vault/session_access.h"

fv_access_mode fv_session_mode(const fv_session_access *access, const fv_vault *vault) {
    return vault->unlocked && fv_access_valid(access->mode) ? access->mode : FV_ACCESS_LOCKED;
}

void fv_session_lock(fv_session_access *access, fv_vault *vault) {
    access->mode = FV_ACCESS_LOCKED;
    ++access->generation;
    fv_vault_lock(vault);
}

fv_vault_result fv_session_unlock(fv_session_access *access, fv_vault *vault,
                                  const fv_vault_platform *platform,
                                  const uint8_t *secret, size_t length, fv_access_mode mode) {
    if (!fv_access_valid(mode)) return FV_VAULT_INVALID;
    if (vault->unlocked || access->mode != FV_ACCESS_LOCKED) return FV_VAULT_DENIED;
    fv_vault_result result = fv_vault_unlock(vault, platform, secret, length);
    if (result != FV_VAULT_OK) {
        fv_session_lock(access, vault);
        return result;
    }
    access->mode = mode;
    ++access->generation;
    return FV_VAULT_OK;
}

fv_vault_result fv_session_reverify_fido(fv_session_access *access, fv_vault *vault,
                                       const uint8_t *secret, size_t length) {
    if (!fv_access_fido(fv_session_mode(access, vault))) return FV_VAULT_DENIED;
    fv_vault_result result = fv_vault_reverify(vault, secret, length);
    if (result != FV_VAULT_OK) fv_session_lock(access, vault);
    return result;
}
