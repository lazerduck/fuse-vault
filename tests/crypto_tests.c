#include "fuse_vault/crypto.h"
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static void reference(fv_algorithm a, const uint8_t *key, uint64_t lba,
                      const uint8_t *input, uint8_t *output) {
    uint8_t iv[16] = {0};
    for (unsigned i=0;i<8;i++) iv[i]=(uint8_t)(lba>>(8*i));
    EVP_CIPHER_CTX *c=EVP_CIPHER_CTX_new(); CHECK(c);
    int n, end;
    if (a == FV_AES_256_XTS) {
        CHECK(EVP_EncryptInit_ex(c,EVP_aes_256_xts(),NULL,key,iv));
        CHECK(EVP_EncryptUpdate(c,output,&n,input,512)); CHECK(n==512);
        CHECK(EVP_EncryptFinal_ex(c,output+n,&end)); CHECK(end==0);
    } else {
        /* Independent Camellia block implementation and explicit polynomial
         * bit multiplication, checked alongside OpenSSL's complete AES-XTS. */
        uint8_t t[16], b[16], next[16];
        CHECK(EVP_EncryptInit_ex(c,EVP_camellia_256_ecb(),NULL,key+32,NULL));
        CHECK(EVP_CIPHER_CTX_set_padding(c,0));
        CHECK(EVP_EncryptUpdate(c,t,&n,iv,16)); CHECK(n==16);
        CHECK(EVP_EncryptInit_ex(c,EVP_camellia_256_ecb(),NULL,key,NULL));
        CHECK(EVP_CIPHER_CTX_set_padding(c,0));
        for (unsigned offset=0;offset<512;offset+=16) {
            for (unsigned j=0;j<16;j++) b[j]=input[offset+j]^t[j];
            CHECK(EVP_EncryptUpdate(c,b,&n,b,16)); CHECK(n==16);
            for (unsigned j=0;j<16;j++) output[offset+j]=b[j]^t[j];
            memset(next,0,16);
            for (unsigned bit=0;bit<127;bit++)
                if ((t[bit/8]>>(bit%8))&1) next[(bit+1)/8] |= (uint8_t)(1u<<((bit+1)%8));
            if (t[15]&128) next[0]^=0x87;
            memcpy(t,next,16);
        }
    }
    EVP_CIPHER_CTX_free(c);
}
int main(void) {
    /* Library's published known-answer cases supplement differential tests. */
    CHECK(mbedtls_aes_self_test(0)==0);
    CHECK(mbedtls_camellia_self_test(0)==0);
    uint8_t keys[4][64], plain[512], encrypted[512], expected[512], temp[513];
    for (unsigned k=0;k<4;k++) for (unsigned i=0;i<64;i++) keys[k][i]=(uint8_t)(i+71*k);
    for (unsigned i=0;i<512;i++) plain[i]=(uint8_t)(i*13+7);
    uint64_t lbas[]={0,1,15,16,UINT64_C(0x123456789abcdef0),UINT64_MAX};
    for (int algorithm=1;algorithm<=2;algorithm++) {
        fv_cipher c={0};
        CHECK(fv_cipher_encrypt(&c,0,plain,temp)==FV_INVALID);
        CHECK(fv_cipher_init(&c,(fv_algorithm)algorithm,keys[0],64)==FV_OK);
        for (unsigned l=0;l<sizeof(lbas)/sizeof(lbas[0]);l++) {
            reference((fv_algorithm)algorithm,keys[0],lbas[l],plain,expected);
            CHECK(fv_cipher_encrypt(&c,lbas[l],plain,encrypted)==FV_OK);
            CHECK(!memcmp(encrypted,expected,512));
            CHECK(fv_cipher_decrypt(&c,lbas[l],encrypted,temp)==FV_OK);
            CHECK(!memcmp(temp,plain,512));
            memcpy(temp,plain,512);
            CHECK(fv_cipher_encrypt(&c,lbas[l],temp,temp)==FV_OK);
            CHECK(!memcmp(temp,expected,512));
            CHECK(fv_cipher_decrypt(&c,lbas[l],temp,temp)==FV_OK);
            CHECK(!memcmp(temp,plain,512));
        }
        CHECK(fv_cipher_encrypt(&c,0,plain,encrypted)==FV_OK);
        CHECK(fv_cipher_encrypt(&c,1,plain,temp)==FV_OK);
        CHECK(memcmp(encrypted,temp,512));
        CHECK(fv_cipher_decrypt(&c,1,encrypted,temp)==FV_OK);
        CHECK(memcmp(temp,plain,512));
        CHECK(fv_cipher_encrypt(&c,0,temp,temp+1)==FV_INVALID);
        CHECK(fv_cipher_encrypt(&c,0,temp+1,temp)==FV_INVALID);
        CHECK(fv_cipher_encrypt(&c,0,NULL,temp)==FV_INVALID);
        fv_cipher_clear(&c);
        for (size_t i=0;i<sizeof(c);i++) CHECK(((uint8_t *)&c)[i]==0);
        CHECK(fv_cipher_init(&c,(fv_algorithm)algorithm,keys[0],63)==FV_INVALID);
        memset(temp,0,64);
        CHECK(fv_cipher_init(&c,(fv_algorithm)algorithm,temp,64)==FV_INVALID);
        CHECK(fv_cipher_init(&c,(fv_algorithm)99,keys[0],64)==FV_INVALID);
    }
    /* All algorithm/order combinations through the configured depth. */
    for (size_t count=1;count<=4;count++) for (unsigned mask=0;mask<(1u<<count);mask++) {
        fv_algorithm algorithms[4]; fv_pipeline p={0};
        memcpy(expected,plain,512);
        for (size_t i=0;i<count;i++) {
            algorithms[i]=(mask&(1u<<i)) ? FV_CAMELLIA_256_XTS : FV_AES_256_XTS;
            reference(algorithms[i],keys[i],17,expected,temp);
            memcpy(expected,temp,512);
        }
        CHECK(fv_pipeline_init(&p,algorithms,(const uint8_t (*)[64])keys,count)==FV_OK);
        CHECK(fv_pipeline_encrypt(&p,17,plain,encrypted)==FV_OK);
        CHECK(!memcmp(encrypted,expected,512));
        CHECK(fv_pipeline_decrypt(&p,17,encrypted,encrypted)==FV_OK);
        CHECK(!memcmp(encrypted,plain,512));
        fv_pipeline_clear(&p);
        for (size_t i=0;i<sizeof(p);i++) CHECK(((uint8_t *)&p)[i]==0);
        CHECK(fv_pipeline_encrypt(&p,17,plain,temp)==FV_INVALID);
    }
    fv_pipeline p={0}; fv_algorithm algorithms[]={FV_AES_256_XTS,FV_CAMELLIA_256_XTS};
    CHECK(fv_pipeline_init(&p,algorithms,(const uint8_t (*)[64])keys,0)==FV_INVALID);
    CHECK(fv_pipeline_init(&p,algorithms,(const uint8_t (*)[64])keys,5)==FV_INVALID);
    algorithms[1]=(fv_algorithm)99;
    CHECK(fv_pipeline_init(&p,algorithms,(const uint8_t (*)[64])keys,2)==FV_INVALID);
    CHECK(p.count==0 && p.layers[0].ops==NULL);
    algorithms[1]=FV_CAMELLIA_256_XTS;
    memcpy(keys[1],keys[0],64);
    CHECK(fv_pipeline_init(&p,algorithms,(const uint8_t (*)[64])keys,2)==FV_INVALID);
    puts("PASS: known answers, independent references, LBA boundaries, in-place buffers, all pipelines, invalid inputs and clearing");
    return 0;
}
