#ifndef FUSE_VAULT_PASSKEYS_H
#define FUSE_VAULT_PASSKEYS_H
#include <stdint.h>
/* Public metadata only; stable resident ID prevents deletion by stale index. */
typedef struct {
    char site[256];
    char account[256];
    uint8_t id[42];
} fv_passkey_t;
typedef enum {
    FV_PASSKEY_BEGIN, FV_PASSKEY_READ, FV_PASSKEY_DELETE, FV_PASSKEY_END
} fv_passkey_action_t;
#endif
