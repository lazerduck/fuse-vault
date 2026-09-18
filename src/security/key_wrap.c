#include "key_wrap.h"
#include "keywrap_compat.h"
#include <mbedtls/aes.h>
#include <mbedtls/camellia.h>
typedef struct {
    uint16_t id;int decrypt;int *error;
    union {mbedtls_aes_context aes;mbedtls_camellia_context camellia;} cipher;
} block_context;
static void crypt(const void *opaque,size_t n,uint8_t *out,const uint8_t *in) {
    const block_context *c=opaque;int r=-1;
    if(n==16 && !*c->error) {
        /* Library ECB interfaces are non-const even with prepared schedules. */
        if(c->id==1)r=mbedtls_aes_crypt_ecb((mbedtls_aes_context *)&c->cipher.aes,
            c->decrypt?MBEDTLS_AES_DECRYPT:MBEDTLS_AES_ENCRYPT,in,out);
        else r=mbedtls_camellia_crypt_ecb((mbedtls_camellia_context *)&c->cipher.camellia,
            c->decrypt?MBEDTLS_CAMELLIA_DECRYPT:MBEDTLS_CAMELLIA_ENCRYPT,in,out);
    }
    if(r){*c->error=-1;memset(out,0,n);}
}
static int run(uint16_t id,const uint8_t *key,const uint8_t *in,uint8_t *out,int decrypt) {
    if(!out)return -1;
    memset(out,0,decrypt?32:40);int error=0,result=-1;
    block_context c={.id=id,.decrypt=decrypt,.error=&error};
    const uint8_t iv[8]={0xa6,0xa6,0xa6,0xa6,0xa6,0xa6,0xa6,0xa6};
    if(!key || !in || (id!=1 && id!=2))goto done;
    if(id==1) {
        mbedtls_aes_init(&c.cipher.aes);
        if(decrypt?mbedtls_aes_setkey_dec(&c.cipher.aes,key,256):mbedtls_aes_setkey_enc(&c.cipher.aes,key,256))goto done;
    } else {
        mbedtls_camellia_init(&c.cipher.camellia);
        if(decrypt?mbedtls_camellia_setkey_dec(&c.cipher.camellia,key,256):mbedtls_camellia_setkey_enc(&c.cipher.camellia,key,256))goto done;
    }
    if(decrypt) {
        if(!fv_nettle_keyunwrap16(&c,crypt,iv,32,out,in))goto done;
    } else fv_nettle_keywrap16(&c,crypt,iv,40,out,in);
    if(!error)result=0;
done:
    mbedtls_platform_zeroize(&c,sizeof(c));
    if(result)mbedtls_platform_zeroize(out,decrypt?32:40);
    return result;
}
int fv_share_wrap(uint16_t id,const uint8_t k[32],const uint8_t in[32],uint8_t out[40]) {return run(id,k,in,out,0);}
int fv_share_unwrap(uint16_t id,const uint8_t k[32],const uint8_t in[40],uint8_t out[32]) {return run(id,k,in,out,1);}
