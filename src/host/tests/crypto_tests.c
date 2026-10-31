#include "fuse_vault/crypto.h"
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)

#include "fixtures/twofish_vectors.h"
static void twofish_vectors(void) {
    fv_twofish_ctx ctx;
    uint8_t zero[16]={0}, out[512], plain[512];
    CHECK(!fv_twofish_setkey(&ctx,tf_kat_key));
    fv_twofish_encrypt(&ctx,zero,out);
    CHECK(!memcmp(out,tf_kat_cipher,16));
    fv_twofish_decrypt(&ctx,tf_kat_cipher,out);
    CHECK(!memcmp(out,zero,16));
    const uint8_t *keys[]={tf_xts_key_0,tf_xts_key_1,tf_xts_key_2};
    const uint8_t *inputs[]={tf_plain_0,tf_plain_1,tf_plain_2};
    const uint8_t *answers[]={tf_cipher_0,tf_cipher_1,tf_cipher_2};
    const uint64_t lbas[]={0,UINT64_C(0x123456789abcdef0),UINT64_MAX};
    for(unsigned i=0;i<3;i++) {
        fv_cipher cipher={0};
        CHECK(fv_cipher_init(&cipher,FV_TWOFISH_256_XTS,keys[i],64)==FV_OK);
        CHECK(fv_cipher_encrypt(&cipher,lbas[i],inputs[i],out)==FV_OK);
        CHECK(!memcmp(out,answers[i],512));
        CHECK(fv_cipher_decrypt(&cipher,lbas[i],answers[i],plain)==FV_OK);
        CHECK(!memcmp(plain,inputs[i],512));
        fv_cipher_clear(&cipher);
    }
}
/* --- Local Twofish-256 reference (supplemented by fixed libgcrypt vectors) for XTS vectors --- */
static uint8_t tf_sbox[2][256];
static int tf_sbox_ready = 0;

static uint8_t tf_gen_q(int qi, uint8_t x) {
    static const uint8_t qbox[2][4][16] = {
        {{0x8,0x1,0x7,0xD,0x6,0xF,0x3,0x2,0x0,0xB,0x5,0x9,0xE,0xC,0xA,0x4},
         {0xE,0xC,0xB,0x8,0x1,0x2,0x3,0x5,0xF,0x4,0xA,0x6,0x7,0x0,0x9,0xD},
         {0xB,0xA,0x5,0xE,0x6,0xD,0x9,0x0,0xC,0x8,0xF,0x3,0x2,0x4,0x7,0x1},
         {0xD,0x7,0xF,0x4,0x1,0x2,0x6,0xE,0x9,0xB,0x3,0x0,0x8,0x5,0xC,0xA}},
        {{0x2,0x8,0xB,0xD,0xF,0x7,0x6,0xE,0x3,0x1,0x9,0x4,0x0,0xA,0xC,0x5},
         {0x1,0xE,0x2,0xB,0x4,0xC,0x3,0x7,0x6,0xD,0xA,0x5,0xF,0x9,0x0,0x8},
         {0x4,0xC,0x7,0x5,0x1,0x6,0x9,0xA,0x0,0xE,0xD,0x8,0x2,0xB,0x3,0xF},
         {0xB,0x9,0x5,0x1,0xC,0x3,0xD,0xE,0x6,0x4,0x7,0xF,0x2,0x0,0x8,0xA}}
    };
    uint8_t a0=(uint8_t)(x/16), b0=(uint8_t)(x%16);
    for (int i=0;i<2;i++) {
        uint8_t a1=(uint8_t)(a0^b0);
        uint8_t b1=(uint8_t)((a0^((b0<<3)|(b0>>1))^(a0<<3))&15);
        a0=qbox[qi][2*i][a1];
        b0=qbox[qi][2*i+1][b1];
    }
    return (uint8_t)((b0<<4)+a0);
}

static void tf_build_sboxes(void) {
    if (tf_sbox_ready) return;
    for (int i=0;i<256;i++) { tf_sbox[0][i]=tf_gen_q(0,(uint8_t)i); tf_sbox[1][i]=tf_gen_q(1,(uint8_t)i); }
    tf_sbox_ready=1;
}

static uint8_t tf_gf_mult_mds(uint8_t a, uint8_t b) {
    uint8_t result=0, aa=a, bb=b;
    for (int i=0;i<8;i++) {
        if (aa&1) result^=bb;
        aa>>=1;
        uint8_t carry=bb&0x80; bb<<=1;
        if (carry) bb^=0x69;
    }
    return result;
}

static uint8_t tf_gf_mult_rs(uint8_t a, uint8_t b) {
    uint8_t result=0, aa=a, bb=b;
    for (int i=0;i<8;i++) {
        if (aa&1) result^=bb;
        aa>>=1;
        uint8_t carry=bb&0x80; bb<<=1;
        if (carry) bb^=0x4d;
    }
    return result;
}

static uint32_t tf_mds_col(uint8_t in, int col) {
    uint8_t m01=in, m5B=tf_gf_mult_mds(in,0x5B), mEF=tf_gf_mult_mds(in,0xEF);
    switch (col) {
    case 0: return (uint32_t)m01|((uint32_t)m5B<<8)|((uint32_t)mEF<<16)|((uint32_t)mEF<<24);
    case 1: return (uint32_t)mEF|((uint32_t)mEF<<8)|((uint32_t)m5B<<16)|((uint32_t)m01<<24);
    case 2: return (uint32_t)m5B|((uint32_t)mEF<<8)|((uint32_t)m01<<16)|((uint32_t)mEF<<24);
    default: return (uint32_t)m5B|((uint32_t)m01<<8)|((uint32_t)mEF<<16)|((uint32_t)m5B<<24);
    }
}

static uint32_t tf_h(const uint8_t in[4], const uint8_t *key, int offset) {
    uint8_t y[4]; memcpy(y,in,4);
    y[0]=(uint8_t)(tf_sbox[1][y[0]]^key[4*(6+offset)+0]);
    y[1]=(uint8_t)(tf_sbox[0][y[1]]^key[4*(6+offset)+1]);
    y[2]=(uint8_t)(tf_sbox[0][y[2]]^key[4*(6+offset)+2]);
    y[3]=(uint8_t)(tf_sbox[1][y[3]]^key[4*(6+offset)+3]);
    y[0]=(uint8_t)(tf_sbox[1][y[0]]^key[4*(4+offset)+0]);
    y[1]=(uint8_t)(tf_sbox[1][y[1]]^key[4*(4+offset)+1]);
    y[2]=(uint8_t)(tf_sbox[0][y[2]]^key[4*(4+offset)+2]);
    y[3]=(uint8_t)(tf_sbox[0][y[3]]^key[4*(4+offset)+3]);
    y[0]=tf_sbox[1][tf_sbox[0][tf_sbox[0][y[0]]^key[4*(2+offset)+0]]^key[4*(0+offset)+0]];
    y[1]=tf_sbox[0][tf_sbox[0][tf_sbox[1][y[1]]^key[4*(2+offset)+1]]^key[4*(0+offset)+1]];
    y[2]=tf_sbox[1][tf_sbox[1][tf_sbox[0][y[2]]^key[4*(2+offset)+2]]^key[4*(0+offset)+2]];
    y[3]=tf_sbox[0][tf_sbox[1][tf_sbox[1][y[3]]^key[4*(2+offset)+3]]^key[4*(0+offset)+3]];
    uint32_t m=0;
    for (int i=0;i<4;i++) m^=tf_mds_col(y[i],i);
    return m;
}

static void tf_ks(const uint8_t key[32], uint32_t sbox[4][256], uint32_t k[40]) {
    static const uint8_t rs[4][8] = {
        {0x01,0xA4,0x55,0x87,0x5A,0x58,0xDB,0x9E},
        {0xA4,0x56,0x82,0xF3,0x1E,0xC6,0x68,0xE5},
        {0x02,0xA1,0xFC,0xC1,0x47,0xAE,0x3D,0x19},
        {0xA4,0x55,0x87,0x5A,0x58,0xDB,0x9E,0x03}
    };
    tf_build_sboxes();
    uint8_t S[16]; memset(S,0,16);
    for (int i=0;i<4;i++) for (int j=0;j<4;j++) for (int kk=0;kk<8;kk++)
        S[4*i+j]^=tf_gf_mult_rs(key[8*i+kk],rs[j][kk]);
    uint8_t tmp[4];
    for (int i=0;i<20;i++) {
        tmp[0]=tmp[1]=tmp[2]=tmp[3]=(uint8_t)(2*i);
        uint32_t A=tf_h(tmp,key,0);
        tmp[0]=tmp[1]=tmp[2]=tmp[3]=(uint8_t)(2*i+1);
        uint32_t B=tf_h(tmp,key,1); B=(B<<8)|(B>>24);
        k[2*i]=A+B;
        uint32_t t=2*B+A; k[2*i+1]=(t<<9)|(t>>23);
    }
    for (int i=0;i<256;i++) {
        uint8_t b=(uint8_t)i;
        sbox[0][i]=tf_mds_col(tf_sbox[1][tf_sbox[0][tf_sbox[0][tf_sbox[1][tf_sbox[1][b]^S[0]]^S[4]]^S[8]]^S[12]],0);
        sbox[1][i]=tf_mds_col(tf_sbox[0][tf_sbox[0][tf_sbox[1][tf_sbox[1][tf_sbox[0][b]^S[1]]^S[5]]^S[9]]^S[13]],1);
        sbox[2][i]=tf_mds_col(tf_sbox[1][tf_sbox[1][tf_sbox[0][tf_sbox[0][tf_sbox[0][b]^S[2]]^S[6]]^S[10]]^S[14]],2);
        sbox[3][i]=tf_mds_col(tf_sbox[0][tf_sbox[1][tf_sbox[1][tf_sbox[0][tf_sbox[1][b]^S[3]]^S[7]]^S[11]]^S[15]],3);
    }
}

static void tf_enc_block(const uint32_t sbox[4][256], const uint32_t k[40],
                         const uint8_t in[16], uint8_t out[16]) {
    const uint32_t *S1=sbox[0],*S2=sbox[1],*S3=sbox[2],*S4=sbox[3];
    uint32_t ia=(uint32_t)in[0]|((uint32_t)in[1]<<8)|((uint32_t)in[2]<<16)|((uint32_t)in[3]<<24);
    uint32_t ib=(uint32_t)in[4]|((uint32_t)in[5]<<8)|((uint32_t)in[6]<<16)|((uint32_t)in[7]<<24);
    uint32_t ic=(uint32_t)in[8]|((uint32_t)in[9]<<8)|((uint32_t)in[10]<<16)|((uint32_t)in[11]<<24);
    uint32_t id=(uint32_t)in[12]|((uint32_t)in[13]<<8)|((uint32_t)in[14]<<16)|((uint32_t)in[15]<<24);
    ia^=k[0]; ib^=k[1]; ic^=k[2]; id^=k[3];
    for (int i=0;i<8;i++) {
        const uint32_t *rk=&k[8+i*4];
        uint32_t t2=S2[(uint8_t)ib]^S3[(uint8_t)(ib>>8)]^S4[(uint8_t)(ib>>16)]^S1[(uint8_t)(ib>>24)];
        uint32_t t1=(S1[(uint8_t)ia]^S2[(uint8_t)(ia>>8)]^S3[(uint8_t)(ia>>16)]^S4[(uint8_t)(ia>>24)])+t2;
        uint32_t x=ic^(t1+rk[0]); ic=(x>>1)|(x<<31);
        id=((id<<1)|(id>>31))^(t2+t1+rk[1]);
        t2=S2[(uint8_t)id]^S3[(uint8_t)(id>>8)]^S4[(uint8_t)(id>>16)]^S1[(uint8_t)(id>>24)];
        t1=(S1[(uint8_t)ic]^S2[(uint8_t)(ic>>8)]^S3[(uint8_t)(ic>>16)]^S4[(uint8_t)(ic>>24)])+t2;
        x=ia^(t1+rk[2]); ia=(x>>1)|(x<<31);
        ib=((ib<<1)|(ib>>31))^(t2+t1+rk[3]);
    }
    uint32_t ta=ic^k[4], tb=id^k[5], tc=ia^k[6], td=ib^k[7];
    out[0]=(uint8_t)ta;out[1]=(uint8_t)(ta>>8);out[2]=(uint8_t)(ta>>16);out[3]=(uint8_t)(ta>>24);
    out[4]=(uint8_t)tb;out[5]=(uint8_t)(tb>>8);out[6]=(uint8_t)(tb>>16);out[7]=(uint8_t)(tb>>24);
    out[8]=(uint8_t)tc;out[9]=(uint8_t)(tc>>8);out[10]=(uint8_t)(tc>>16);out[11]=(uint8_t)(tc>>24);
    out[12]=(uint8_t)td;out[13]=(uint8_t)(td>>8);out[14]=(uint8_t)(td>>16);out[15]=(uint8_t)(td>>24);
}

static void tf_xts_encrypt(const uint8_t key[64], uint64_t lba,
                           const uint8_t *input, uint8_t *output) {
    uint8_t iv[16]={0};
    for (unsigned i=0;i<8;i++) iv[i]=(uint8_t)(lba>>(8*i));
    uint32_t dsbox[4][256], tsbox[4][256];
    uint32_t dk[40], tk[40];
    tf_ks(key, dsbox, dk);
    tf_ks(key+32, tsbox, tk);
    uint8_t t[16], b[16], next[16];
    tf_enc_block(tsbox, tk, iv, t);
    for (unsigned offset=0;offset<512;offset+=16) {
        for (unsigned j=0;j<16;j++) b[j]=input[offset+j]^t[j];
        tf_enc_block(dsbox, dk, b, b);
        for (unsigned j=0;j<16;j++) output[offset+j]=b[j]^t[j];
        memset(next,0,16);
        for (unsigned bit=0;bit<127;bit++)
            if ((t[bit/8]>>(bit%8))&1) next[(bit+1)/8]|=(uint8_t)(1u<<((bit+1)%8));
        if (t[15]&128) next[0]^=0x87;
        memcpy(t,next,16);
    }
}
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
    } else if (a == FV_TWOFISH_256_XTS) {
        tf_xts_encrypt(key, lba, input, output);
    } else {
        /* Independent Camellia/SM4 block implementation and explicit polynomial
         * bit multiplication, checked alongside OpenSSL's complete AES-XTS. */
        uint8_t t[16], b[16], next[16];
        const EVP_CIPHER *cipher = a == FV_SM4_128_XTS ? EVP_sm4_ecb() :
                           a == FV_ARIA_256_XTS ? EVP_aria_256_ecb() :
                           EVP_camellia_256_ecb();
        CHECK(EVP_EncryptInit_ex(c,cipher,NULL,key+(a == FV_SM4_128_XTS ? 16 : 32),NULL));
        CHECK(EVP_CIPHER_CTX_set_padding(c,0));
        CHECK(EVP_EncryptUpdate(c,t,&n,iv,16)); CHECK(n==16);
        CHECK(EVP_EncryptInit_ex(c,cipher,NULL,key,NULL));
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
    /* GB/T 32907 SM4 single-block and million-iteration known answers. */
    const uint8_t k[16]={0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10};
    const uint8_t answer[16]={0x68,0x1e,0xdf,0x34,0xd2,0x06,0x96,0x5e,0x86,0xb3,0xe9,0x4f,0x53,0x6e,0x42,0x46};
    const uint8_t million[16]={0x59,0x52,0x98,0xc7,0xc6,0xfd,0x27,0x1f,0x04,0x02,0xf8,0x04,0xc3,0x3d,0x3f,0x66};
    SM4_KEY schedule; uint8_t block[16];
    sm4_set_encrypt_key(&schedule,k);sm4_encrypt(&schedule,k,block);CHECK(!memcmp(block,answer,16));
    for(unsigned i=1;i<1000000;i++)sm4_encrypt(&schedule,block,block);
    CHECK(!memcmp(block,million,16));
    sm4_set_decrypt_key(&schedule,k);sm4_encrypt(&schedule,answer,block);CHECK(!memcmp(block,k,16));
    twofish_vectors();
    /* Library's published known-answer cases supplement differential tests. */
    CHECK(mbedtls_aes_self_test(0)==0);
    CHECK(mbedtls_camellia_self_test(0)==0);
    uint8_t keys[4][64], plain[512], encrypted[512], expected[512], temp[513];
    for (unsigned k=0;k<4;k++) for (unsigned i=0;i<64;i++) keys[k][i]=(uint8_t)(i+71*k);
    for (unsigned i=0;i<512;i++) plain[i]=(uint8_t)(i*13+7);
    uint64_t lbas[]={0,1,15,16,UINT64_C(0x123456789abcdef0),UINT64_MAX};
    for (int algorithm=1;algorithm<=5;algorithm++) {
        fv_cipher c={0};
        CHECK(fv_cipher_encrypt(&c,0,plain,temp)==FV_INVALID);
        CHECK(fv_cipher_init(&c,(fv_algorithm)algorithm,keys[0],fv_cipher_key_bytes((fv_algorithm)algorithm))==FV_OK);
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
        CHECK(fv_cipher_init(&c,(fv_algorithm)algorithm,temp,fv_cipher_key_bytes((fv_algorithm)algorithm))==FV_INVALID);
        CHECK(fv_cipher_init(&c,(fv_algorithm)99,keys[0],64)==FV_INVALID);
    }
    /* All algorithm/order combinations through the configured depth. */
    unsigned combinations=1;
    for (size_t count=1;count<=4;count++) {
        combinations*=5;
        for (unsigned mask=0;mask<combinations;mask++) {
            unsigned digits=mask;
            fv_algorithm algorithms[4]; fv_pipeline p={0};
            memcpy(expected,plain,512);
            for (size_t i=0;i<count;i++) {
                algorithms[i]=(fv_algorithm)(1+digits%5); digits/=5;
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
    /* Effective-key validation must not depend on unused capacity bytes. */
    algorithms[0]=algorithms[1]=FV_SM4_128_XTS;
    keys[1][63]^=1;
    CHECK(fv_pipeline_init(&p,algorithms,(const uint8_t (*)[64])keys,2)==FV_INVALID);
    fv_cipher c={0};
    CHECK(fv_cipher_init(&c,FV_SM4_128_XTS,keys[0],64)==FV_INVALID);
    CHECK(fv_cipher_init(&c,FV_SM4_128_XTS,keys[0],32)==FV_OK);
    CHECK(fv_cipher_encrypt(&c,0,plain,expected)==FV_OK);fv_cipher_clear(&c);
    CHECK(fv_pipeline_init(&p,algorithms,(const uint8_t (*)[64])keys,1)==FV_OK);
    CHECK(fv_pipeline_encrypt(&p,0,plain,encrypted)==FV_OK);CHECK(!memcmp(encrypted,expected,512));
    fv_pipeline_clear(&p);keys[0][50]^=1;
    CHECK(fv_pipeline_init(&p,algorithms,(const uint8_t (*)[64])keys,1)==FV_OK);
    CHECK(fv_pipeline_encrypt(&p,0,plain,encrypted)==FV_OK);CHECK(!memcmp(encrypted,expected,512));
    fv_pipeline_clear(&p);
    memcpy(keys[0]+16,keys[0],16);
    CHECK(fv_cipher_init(&c,FV_SM4_128_XTS,keys[0],32)==FV_INVALID);
    puts("PASS: known answers, independent references, LBA boundaries, in-place buffers, all pipelines, invalid inputs and clearing");
    return 0;
}
