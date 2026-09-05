#ifndef FUSE_VAULT_SECURITY_JOURNAL_H
#define FUSE_VAULT_SECURITY_JOURNAL_H

#include "fuse_vault/persistence.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FV_JOURNAL_RECORD_SIZE 256u
#define FV_JOURNAL_TAG_SIZE 32u
#define FV_JOURNAL_AUTHENTICATED_SIZE 128u
#define FV_JOURNAL_COMMITTED_SIZE \
    (FV_JOURNAL_AUTHENTICATED_SIZE + (2u * FV_JOURNAL_TAG_SIZE))
#define FV_JOURNAL_MINIMUM_ERASE_BLOCKS 2u

typedef enum {
    FV_JOURNAL_OK = 0,
    FV_JOURNAL_EMPTY,
    FV_JOURNAL_INVALID_ARGUMENT,
    FV_JOURNAL_IO_ERROR,
    FV_JOURNAL_AUTHENTICATION_ERROR,
    FV_JOURNAL_SEQUENCE_ERROR,
} fv_journal_result_t;

typedef struct fv_journal_flash fv_journal_flash_t;

typedef struct {
    bool (*read)(fv_journal_flash_t *flash, size_t offset,
                 uint8_t *output, size_t length);
    bool (*program)(fv_journal_flash_t *flash, size_t offset,
                    const uint8_t *input, size_t length);
    bool (*erase)(fv_journal_flash_t *flash, size_t offset, size_t length);
} fv_journal_flash_ops_t;

struct fv_journal_flash {
    const fv_journal_flash_ops_t *ops;
    void *context;
    size_t size;
    size_t erase_block_size;
    size_t program_size;
};

typedef struct fv_journal_authenticator fv_journal_authenticator_t;

typedef struct {
    bool (*compute_tags)(fv_journal_authenticator_t *authenticator,
                         const uint8_t *record, size_t authenticated_length,
                         uint8_t tag_a[FV_JOURNAL_TAG_SIZE],
                         uint8_t tag_b[FV_JOURNAL_TAG_SIZE]);
} fv_journal_authenticator_ops_t;

struct fv_journal_authenticator {
    const fv_journal_authenticator_ops_t *ops;
    void *context;
};

typedef struct {
    uint64_t sequence;
    uint64_t previous_sequence;
    uint8_t vault_id[FV_VAULT_ID_SIZE];
    uint8_t failed_attempts;
    bool provisioned;
} fv_journal_state_t;

typedef struct {
    fv_journal_flash_t *flash;
    fv_journal_authenticator_t *authenticator;
} fv_security_journal_t;

bool fv_security_journal_init(fv_security_journal_t *journal,
                              fv_journal_flash_t *flash,
                              fv_journal_authenticator_t *authenticator);
fv_journal_result_t fv_security_journal_recover(
    fv_security_journal_t *journal, fv_journal_state_t *state);
fv_journal_result_t fv_security_journal_append(
    fv_security_journal_t *journal, const fv_journal_state_t *state);

#endif
