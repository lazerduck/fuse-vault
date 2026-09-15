#ifndef FUSE_VAULT_FIDO_STORE_H
#define FUSE_VAULT_FIDO_STORE_H
#include "fuse_vault/fido_engine.h"
#include "fuse_vault/encrypted_block.h"
#include "fuse_vault/block_slice.h"
#include "fuse_vault/media_layout.h"
/* Owns a private encrypted SD slice. Never export this interface through MSC.
 * Close the engine before clearing this owner, because the engine borrows image. */
typedef struct {
    fv_platform_services_t *services;
    fv_block_slice_t slice;
    fv_encrypted_block_t encrypted;
    uint8_t image[FV_FIDO_STORE_BYTES];
    uint8_t root_key[32];
    uint8_t digest[32];
    unsigned active_bank;
    bool ready;
    bool (*progress)(void *context);
    void *progress_context;
} fv_fido_store_t;
bool fv_fido_store_open(fv_fido_store_t *store, fv_platform_services_t *services,
    fv_block_device_t *media, const fv_media_layout_t *layout,
    const fv_volume_master_key_t *vmk,
    const fv_encryption_stack_descriptor_t *stack);
bool fv_fido_store_commit(void *context, const uint8_t *image, size_t size);
void fv_fido_store_close(fv_fido_store_t *store);
#endif
