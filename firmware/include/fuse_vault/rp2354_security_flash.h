#ifndef FUSE_VAULT_RP2354_SECURITY_FLASH_H
#define FUSE_VAULT_RP2354_SECURITY_FLASH_H

#include "fuse_vault/security_journal.h"

#include <stdbool.h>

/*
 * Bind a journal flash interface to the final two sectors of RP2354A stacked
 * flash. Returns false if the firmware image overlaps the reserved region or
 * if the board's flash geometry does not match the journal invariants.
 */
bool fv_rp2354_security_flash_init(fv_journal_flash_t *flash);

#endif
