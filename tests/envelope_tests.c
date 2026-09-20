#include "fuse_vault/envelope.h"
#include "../src/security/key_wrap.h"
#include "fixtures/envelope_vectors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static bool zero(const void *p,size_t n){const uint8_t *b=p;while(n--)if(*b++)return false;return true;}
typedef struct {unsigned next,calls,fail;} random_state;
static int random_bytes(void *ctx,uint8_t *out,size_t n) {
    random_state *s=ctx;
    if(++s->calls==s->fail){memset(out,0x55,n);return -1;}
    for(size_t i=0;i<n;i++)out[i]=(uint8_t)s->next++;
    return 0;
}
static const uint8_t secret[]={'a',0,'t','e','s','t',' ','p','a','s','s','w','o','r','d'};
static void vector(const uint8_t expected[512],const uint8_t bad_kw[512],const uint8_t expected_binding[32]) {
    fv_kdf_limits limits={1,100};fv_envelope_config c;
    uint8_t root[32],token[32],vmk[32],binding[32],out[32],header[512],bad[512];
    for(unsigned i=0;i<32;i++){root[i]=i;token[i]=32+i;vmk[i]=64+i;}
    CHECK(fv_envelope_parse(expected,512,2133,limits,&c));
    CHECK(!fv_vault_binding(root,token,9,c.volume.volume_id,binding));CHECK(!memcmp(binding,expected_binding,32));
    random_state rng={0};
    CHECK(!fv_envelope_seal(&c,2133,limits,binding,secret,sizeof(secret),vmk,random_bytes,&rng,header));
    CHECK(!memcmp(header,expected,512));CHECK(rng.calls==c.volume.layer_count);
    CHECK(!fv_envelope_open(header,2133,limits,binding,secret,sizeof(secret),out));CHECK(!memcmp(out,vmk,32));
    for(unsigned i=0;i<512;i++) {
        memcpy(bad,header,512);bad[i]^=1;memset(out,1,32);
        CHECK(fv_envelope_open(bad,2133,limits,binding,secret,sizeof(secret),out));CHECK(zero(out,32));
    }
    CHECK(fv_envelope_open(bad_kw,2133,limits,binding,secret,sizeof(secret),out));CHECK(zero(out,32));
    binding[0]^=1;CHECK(fv_envelope_open(header,2133,limits,binding,secret,sizeof(secret),out));CHECK(zero(out,32));binding[0]^=1;
    CHECK(fv_envelope_open(header,2133,limits,binding,(const uint8_t*)"bad",3,out));CHECK(zero(out,32));
    CHECK(!fv_envelope_parse(header,511,2133,limits,&c));CHECK(zero(&c,sizeof(c)));
    CHECK(!fv_envelope_parse(header,512,2132,limits,&c));
    CHECK(!fv_envelope_parse(header,512,2133,(fv_kdf_limits){4,100},&c));
    CHECK(!fv_envelope_parse(header,512,2133,(fv_kdf_limits){1,2},&c));
    CHECK(fv_envelope_parse(header,512,2133,limits,&c));
    for(unsigned fail=1;fail<=c.volume.layer_count;fail++) {
        rng=(random_state){.fail=fail};
        CHECK(fv_envelope_seal(&c,2133,limits,binding,secret,sizeof(secret),vmk,random_bytes,&rng,bad));CHECK(zero(bad,512));
    }
    c.credential_profile=2;rng=(random_state){0};
    CHECK(fv_envelope_seal(&c,2133,limits,binding,secret,sizeof(secret),vmk,random_bytes,&rng,bad));CHECK(!rng.calls);
    const uint8_t navigation[]={1,5,2,4,3};
    CHECK(!fv_envelope_seal(&c,2133,limits,binding,navigation,5,vmk,random_bytes,&rng,bad));
    CHECK(!fv_envelope_open(bad,2133,limits,binding,navigation,5,out));CHECK(!memcmp(out,vmk,32));
    for(unsigned profile=3;profile<=4;profile++){
        uint8_t chosen[4]={0,1,2,(uint8_t)(profile==3?99:63)};
        c.credential_profile=(uint16_t)profile;rng=(random_state){0};
        CHECK(!fv_envelope_seal(&c,2133,limits,binding,chosen,4,vmk,random_bytes,&rng,bad));
        CHECK(!fv_envelope_open(bad,2133,limits,binding,chosen,4,out));CHECK(!memcmp(out,vmk,32));
        CHECK(fv_envelope_open(bad,2133,limits,binding,chosen,3,out));
        chosen[3]++;rng=(random_state){0};
        CHECK(fv_envelope_seal(&c,2133,limits,binding,chosen,4,vmk,random_bytes,&rng,bad));CHECK(!rng.calls);
    }
}
static void wrap_vector(uint16_t id,const uint8_t key[32],const uint8_t share[32],const uint8_t expected[40]) {
    uint8_t wrapped[40],out[32];CHECK(!fv_share_wrap(id,key,share,wrapped));CHECK(!memcmp(wrapped,expected,40));
    CHECK(!fv_share_unwrap(id,key,wrapped,out));CHECK(!memcmp(out,share,32));
    for(unsigned i=0;i<40;i++){wrapped[i]^=1;CHECK(fv_share_unwrap(id,key,wrapped,out));CHECK(zero(out,32));wrapped[i]^=1;}
}
int main(void) {
    vector(aes_header,aes_bad_kw,aes_binding);vector(cam_header,cam_bad_kw,cam_binding);
    vector(pair_header,pair_bad_kw,pair_binding);vector(three_header,three_bad_kw,three_binding);vector(four_header,four_bad_kw,four_binding);
    wrap_vector(1,aes_key_0,aes_share_0,aes_wrapped_0);wrap_vector(2,cam_key_0,cam_share_0,cam_wrapped_0);
    puts("Envelope: independent fixed vectors, full-byte tampering, KW failure, RNG failure, cost/profile bounds passed");
    return 0;
}
