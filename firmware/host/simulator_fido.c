#include "simulator_fido.h"
#include <string.h>

static bool random_bytes(void *context, uint8_t *out, size_t size) {
    fv_simulator_fido_t *f = context;
    return f->runtime->services->ops->random_fill(f->runtime->services, out, size);
}
static bool commit(void *context, const uint8_t *image, size_t size) {
    fv_simulator_fido_t *f = context;
    bool ok = fv_fido_store_commit(&f->store, image, size);
    if (!ok) f->failed = true;
    return ok;
}
static uint32_t millis(void *context) {
    fv_simulator_fido_t *f = context;
    return f->millis(f->context);
}
static int presence(void *context) {
    fv_simulator_fido_t *f = context;
    return f->presence(f->context, f->command == 7u);
}
static bool local_authorized(void *context) {
    fv_simulator_fido_t *f = context;
    return f->attached && f->runtime->authentication.vmk_valid &&
        f->runtime->app->session_unlocked && f->verification.valid &&
        (uint32_t)(millis(f) - f->verification.verified_at) < FV_FIDO_UV_COMPLETE_MS;
}
static bool verify_user(void *context, const uint8_t *rp_hash) {
    fv_simulator_fido_t *f = context;
    return local_authorized(f) &&
        fv_fido_verification_use(&f->verification, millis(f), rp_hash);
}
static uint8_t retries(void *context) {
    fv_simulator_fido_t *f = context;
    return (uint8_t)(FV_MAX_UNLOCK_ATTEMPTS - f->runtime->app->failed_attempts);
}
void fv_simulator_fido_init(fv_simulator_fido_t *f,
    fv_device_runtime_t *runtime, uint32_t (*clock)(void *),
    int (*approve)(void *, bool), void *context) {
    memset(f, 0, sizeof(*f));
    f->runtime = runtime; f->millis = clock; f->presence = approve; f->context = context;
}
void fv_simulator_fido_close(fv_simulator_fido_t *f) {
    /* The upstream engine is a singleton. Only its owning adapter closes it. */
    if (f->attached) fv_fido_engine_close();
    fv_fido_store_close(&f->store);
    fv_fido_verification_clear(&f->verification);
    f->attached = false;
}
void fv_simulator_fido_session(fv_simulator_fido_t *f, bool was_unlocked) {
    if (!f->runtime->app->session_unlocked) fv_fido_verification_clear(&f->verification);
    else if (!was_unlocked) fv_fido_verification_begin(&f->verification, millis(f));
}
bool fv_simulator_fido_attach(fv_simulator_fido_t *f,
    const fv_volume_master_key_t *vmk, const fv_media_layout_t *layout,
    const fv_encryption_stack_descriptor_t *stack) {
    f->failed = false;
    if (!fv_fido_store_open(&f->store, f->runtime->services, f->runtime->raw_media,
        layout, vmk, stack)) return false;
    fv_fido_engine_ops_t ops = {.random = random_bytes, .commit = commit,
        .presence = presence, .millis = millis, .verify_user = verify_user,
        .uv_retries = retries, .local_authorized = local_authorized, .context = f};
    /* A vault ID is stable across simulator restarts and password changes. */
    if (!fv_fido_engine_open(f->store.image, f->store.root_key, layout->vault_id, &ops)) {
        fv_fido_store_close(&f->store);
        return false;
    }
    f->attached = true;
    return true;
}
bool fv_simulator_fido_manage(fv_simulator_fido_t *f,
    fv_passkey_action_t action, uint16_t *index, uint16_t *count, fv_passkey_t *entry) {
    return f->attached && fv_fido_engine_manage(action, index, count, entry);
}
size_t fv_simulator_fido_command(fv_simulator_fido_t *f,
    const uint8_t *request, size_t size, uint8_t *response, size_t capacity) {
    if (!f->attached || !size || !capacity) return 0;
    f->command = request[0];
    size_t result = fv_fido_engine_command(request, size, response, capacity);
    f->command = 0;
    return result;
}
