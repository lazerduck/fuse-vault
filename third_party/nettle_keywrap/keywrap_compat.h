#ifndef FV_KEYWRAP_COMPAT_H
#define FV_KEYWRAP_COMPAT_H
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <mbedtls/platform_util.h>
#define fv_kw_zero mbedtls_platform_zeroize
#define nist_keywrap16 fv_nettle_keywrap16
#define nist_keyunwrap16 fv_nettle_keyunwrap16
typedef void nettle_cipher_func(const void *,size_t,uint8_t *,const uint8_t *);
union nettle_block16 {uint8_t b[16];uint64_t u64[2];};
union nettle_block8 {uint8_t b[8];uint64_t u64;};
static inline uint64_t bswap64_if_le(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(x);
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return x;
#else
#error Unsupported byte order
#endif
}
static inline int memeql_sec(const void *a,const void *b,size_t n) {
    const uint8_t *x=a,*y=b;unsigned difference=0;
    for(size_t i=0;i<n;i++)difference|=x[i]^y[i];
    return difference==0;
}
void fv_nettle_keywrap16(const void *,nettle_cipher_func *,const uint8_t *,size_t,uint8_t *,const uint8_t *);
int fv_nettle_keyunwrap16(const void *,nettle_cipher_func *,const uint8_t *,size_t,uint8_t *,const uint8_t *);
#endif
