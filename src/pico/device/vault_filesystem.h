#ifndef FV_VAULT_FILESYSTEM_H
#define FV_VAULT_FILESYSTEM_H
#include "fuse_vault/vault.h"
/* First creation only, with EMPTY authority. Single worker, no concurrent FatFs
 * operations. Uses the supplied aligned workspace; never formats the raw SD. */
int fv_vault_format_fat32(fv_vault *, uint8_t *workspace, size_t bytes);
#endif
