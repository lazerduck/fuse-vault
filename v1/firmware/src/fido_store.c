#include "fuse_vault/fido_store.h"
#include "fuse_vault/journal_authenticator.h"
#include <string.h>

#define BANK_BLOCKS (FV_FIDO_STORE_BYTES / FV_BLOCK_SIZE)
_Static_assert(FV_FIDO_STORE_BYTES % FV_BLOCK_SIZE == 0, "sector aligned snapshot");
static void clear(void *data, size_t size) {
    volatile uint8_t *p = data;
    while (size-- != 0u) *p++ = 0;
}
static bool random_fill(void *context, uint8_t *out, size_t size) {
    fv_fido_store_t *store = context;
    return store->services->ops->random_fill(store->services, out, size);
}
static bool digest_image(fv_fido_store_t *store, const uint8_t *image, uint8_t out[32]) {
    static const uint8_t label[] = "Fuse Vault FIDO snapshot v1";
    return fv_kmac256(store->root_key, 32, image, FV_FIDO_STORE_BYTES,
                       label, sizeof(label) - 1u, out, 32u);
}
void fv_fido_store_close(fv_fido_store_t *store) {
    if (store == NULL) return;
    fv_encrypted_block_lock(&store->encrypted);
    clear(store, sizeof(*store));
}
bool fv_fido_store_open(fv_fido_store_t *store, fv_platform_services_t *services,
    fv_block_device_t *media, const fv_media_layout_t *layout,
    const fv_volume_master_key_t *vmk,
    const fv_encryption_stack_descriptor_t *stack) {
    if (store == NULL) return false;
    clear(store, sizeof(*store));
    if (services == NULL || services->ops == NULL ||
        services->ops->random_fill == NULL ||
        services->ops->load_security_state == NULL ||
        services->ops->store_security_state == NULL || vmk == NULL || layout == NULL) return false;
    store->services = services;
    fv_security_state_t state = {0};
    fv_volume_master_key_t storage_key = {0};
    static const uint8_t storage_label[] = "Fuse Vault FIDO SD encryption v1";
    static const uint8_t engine_label[] = "Fuse Vault FIDO engine root v1";
    bool ok = services->ops->load_security_state(services, &state) == FV_PERSIST_OK &&
        state.provisioned &&
        fv_hmac_sha256(vmk->bytes, sizeof(vmk->bytes), storage_label,
            sizeof(storage_label) - 1u, layout->vault_id, FV_VAULT_ID_SIZE, storage_key.bytes) &&
        fv_hmac_sha256(vmk->bytes, sizeof(vmk->bytes), engine_label,
            sizeof(engine_label) - 1u, layout->vault_id, FV_VAULT_ID_SIZE, store->root_key) &&
        fv_media_open_fido(layout, media, &store->slice) &&
        fv_encrypted_block_init(&store->encrypted, &store->slice.interface,
            &storage_key, layout->vault_id, stack, random_fill, store);
    fv_volume_master_key_clear(&storage_key);
    if (ok && store->encrypted.logical_blocks < 2u * BANK_BLOCKS) ok = false;
    if (ok && !state.fido_initialized) {
        memset(store->image, 0xff, sizeof(store->image));
        store->active_bank = 1u; /* first commit goes to bank zero */
    } else if (ok) {
        ok = false;
        fv_block_device_t *blocks = &store->encrypted.interface;
        for (unsigned bank = 0; bank < 2u; ++bank) {
            uint8_t digest[32] = {0};
            bool read_ok = true;
            for (uint32_t i = 0; read_ok && i < BANK_BLOCKS; ++i) {
                read_ok = blocks->ops->read(blocks, (uint64_t)bank * BANK_BLOCKS + i,
                    1u, store->image + (size_t)i * FV_BLOCK_SIZE) == FV_BLOCK_OK;
            }
            if (read_ok &&
                digest_image(store, store->image, digest) &&
                memcmp(digest, state.fido_digest, sizeof(digest)) == 0) {
                store->active_bank = bank;
                memcpy(store->digest, digest, sizeof(digest));
                ok = true;
            }
            clear(digest, sizeof(digest));
            if (ok) break;
        }
    }
    clear(&state, sizeof(state));
    if (!ok) { fv_fido_store_close(store); return false; }
    store->ready = true;
    return true;
}
bool fv_fido_store_commit(void *context, const uint8_t *image, size_t size) {
    fv_fido_store_t *store = context;
    if (store == NULL || !store->ready || image == NULL || size != FV_FIDO_STORE_BYTES) return false;
    fv_security_state_t state = {0};
    uint8_t digest[32] = {0};
    fv_block_device_t *blocks = &store->encrypted.interface;
    const unsigned next_bank = store->active_bank ^ 1u;
    bool ok = store->services->ops->load_security_state(store->services, &state) == FV_PERSIST_OK &&
        state.provisioned && state.sequence != UINT64_MAX &&
        (!state.fido_initialized || memcmp(state.fido_digest, store->digest, 32) == 0) &&
        digest_image(store, image, digest);
    for (uint32_t i = 0; ok && i < BANK_BLOCKS; ++i) {
        if (store->progress && !store->progress(store->progress_context)) { ok = false; break; }
        ok = blocks->ops->write(blocks, (uint64_t)next_bank * BANK_BLOCKS + i,
            1u, image + (size_t)i * FV_BLOCK_SIZE) == FV_BLOCK_OK;
    }
    if (ok) ok = blocks->ops->sync(blocks) == FV_BLOCK_OK;
    if (ok) {
        ++state.sequence;
        state.fido_initialized = true;
        memcpy(state.fido_digest, digest, sizeof(digest));
        ok = store->services->ops->store_security_state(store->services, &state) == FV_PERSIST_OK;
        if (ok) {
            fv_security_state_t verified = {0};
            ok = store->services->ops->load_security_state(store->services, &verified) == FV_PERSIST_OK &&
                verified.sequence == state.sequence && verified.fido_initialized &&
                memcmp(verified.fido_digest, digest, sizeof(digest)) == 0;
            clear(&verified, sizeof(verified));
        }
    }
    if (ok) {
        store->active_bank = next_bank;
        memcpy(store->digest, digest, sizeof(digest));
    } else {
        /* Keep the borrowed image intact until the engine unwinds. */
        store->ready = false;
    }
    clear(digest, sizeof(digest));
    clear(&state, sizeof(state));
    return ok;
}
