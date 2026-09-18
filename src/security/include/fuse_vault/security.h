#ifndef FV_SECURITY_H
#define FV_SECURITY_H
#include "fuse_vault/volume_format.h"
#define FV_AUTH_POLICY_BYTES 16u
/* Only the first two actions are supported. Other IDs are reserved, rejected. */
typedef enum { FV_LIMIT_DESTROY=1,FV_LIMIT_LOCKOUT=2,
    FV_LIMIT_DELAY_RESERVED=3,FV_LIMIT_TIMED_LOCKOUT_RESERVED=4 } fv_limit_action;
typedef struct {uint32_t max_attempts;fv_limit_action limit_action;} fv_auth_policy;
fv_auth_policy fv_auth_policy_default(void);
bool fv_auth_policy_encode(const fv_auth_policy *,uint8_t out[FV_AUTH_POLICY_BYTES]);
bool fv_auth_policy_decode(const uint8_t *,size_t,fv_auth_policy *);

/* RFC 5869, SHA-256. Empty salt means the RFC's 32 zero bytes.
 * Expand supports <=255*32 output bytes and <=256 info bytes.
 * All buffers must be disjoint. On error, valid output spans are zeroed;
 * invalid expand output lengths (zero or >8160) are rejected without writing. */
int fv_hkdf_extract(const uint8_t *salt,size_t salt_bytes,const uint8_t *ikm,size_t ikm_bytes,uint8_t prk[32]);
int fv_hkdf_expand(const uint8_t prk[32],const uint8_t *info,size_t info_bytes,uint8_t *out,size_t out_bytes);
/* RFC 8018 PBKDF2-HMAC-SHA256, fixed 32-byte output. iterations must be >0.
 * Caller MUST apply trusted KDF cost limits before invoking this primitive. */
int fv_pbkdf2_sha256_32(const uint8_t *password,size_t password_bytes,
    const uint8_t *salt,size_t salt_bytes,uint32_t iterations,uint8_t out[32]);

typedef struct {uint8_t layers[FV_MAX_LAYERS][64];uint8_t integrity[32];} fv_working_keys;
void fv_working_keys_clear(fv_working_keys *);
/* Only call with an authenticated descriptor and recovered VMK. Syntax
 * validation here is not authentication. No wrapper or root access is provided. */
int fv_derive_working_keys(const uint8_t vmk[32],const fv_volume_descriptor *,fv_working_keys *);
#endif
