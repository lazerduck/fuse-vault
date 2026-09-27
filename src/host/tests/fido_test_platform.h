#ifndef FV_FIDO_TEST_PLATFORM_H
#define FV_FIDO_TEST_PLATFORM_H
/* Test-only file-backed media and in-memory authority. Never firmware sources. */
#include "fuse_vault/fido_store.h"
#include <stdio.h>
#define FV_TEST_CAPACITY 2300u
typedef struct {
    FILE *media;
    uint8_t *durable;
    fv_block_device_t device;
    fv_vault_platform platform;
    fv_device_state authority;
    fv_vault vault;
    bool present, destroyed, credential_changed;
    unsigned entropy, writes, syncs, reads;
    unsigned fail_write, tear_bytes, fail_sync, fail_read, corrupt_write;
    uint64_t last_lba;
} fv_fido_fixture;
void fv_fido_fixture_init(fv_fido_fixture *,const uint16_t algorithms[4],unsigned layers);
void fv_fido_fixture_close(fv_fido_fixture *);
void fv_fido_fixture_unlock(fv_fido_fixture *);
void fv_fido_fixture_change(fv_fido_fixture *);
void fv_fido_fixture_io_reset(fv_fido_fixture *);
void fv_fido_fixture_checkpoint(fv_fido_fixture *);
void fv_fido_fixture_power_cut(fv_fido_fixture *,bool discard_unsynchronized);
#endif
