#include "fuse_vault/vault_header_store.h"
#include "fuse_vault/credential_envelope.h"
#include "fuse_vault/journal_authenticator.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) check((x),#x,__FILE__,__LINE__)
static void check(bool ok,const char*x,const char*f,int l){if(!ok){fprintf(stderr,"%s:%d: check failed: %s\n",f,l,x);exit(EXIT_FAILURE);}}
typedef struct{uint8_t bytes[1024];size_t cut;bool fail_sync;bool short_read;} memory_t;
static bool valid_range(uint64_t f,uint32_t n){return n>0u&&f<2u&&(uint64_t)n<=2u-f;}
static fv_block_result_t rd(fv_block_device_t*d,uint64_t f,uint32_t n,uint8_t*out){memory_t*m=d->context;if(!out)return FV_BLOCK_ERROR_INVALID_ARGUMENT;if(!valid_range(f,n))return FV_BLOCK_ERROR_OUT_OF_RANGE;if(m->short_read){memcpy(out,m->bytes+(size_t)f*512u,511u);return FV_BLOCK_ERROR_IO;}memcpy(out,m->bytes+(size_t)f*512u,(size_t)n*512u);return FV_BLOCK_OK;}
static fv_block_result_t wr(fv_block_device_t*d,uint64_t f,uint32_t n,const uint8_t*in){memory_t*m=d->context;if(!in)return FV_BLOCK_ERROR_INVALID_ARGUMENT;if(!valid_range(f,n))return FV_BLOCK_ERROR_OUT_OF_RANGE;size_t length=(size_t)n*512u,amount=m->cut<length?m->cut:length;memcpy(m->bytes+(size_t)f*512u,in,amount);return amount==length?FV_BLOCK_OK:FV_BLOCK_ERROR_IO;}
static fv_block_result_t sy(fv_block_device_t*d){memory_t*m=d->context;return m->fail_sync?FV_BLOCK_ERROR_IO:FV_BLOCK_OK;}
static uint64_t bc(const fv_block_device_t*d){(void)d;return 2u;}static bool ip(const fv_block_device_t*d){(void)d;return true;}
static const fv_block_device_ops_t OPS={rd,wr,sy,bc,ip};
static fv_vault_header_t header(uint64_t sequence){fv_vault_header_t h={.sequence=sequence,.crypto_profile=FV_CRYPTO_PROFILE_DUAL_FAMILY_V1,.entry_method=FV_SECRET_METHOD_DIRECTIONS_V1,.branch_a_cost=0x00020304u,.branch_b_cost=0x00060708u,.wrapped_vmk_length=FV_CREDENTIAL_ENVELOPE_SIZE};for(size_t i=0;i<16;i++){h.vault_id[i]=(uint8_t)(i+1u);h.branch_a_salt[i]=(uint8_t)(0x20u+i);h.branch_b_salt[i]=(uint8_t)(0x40u+i);}for(size_t i=0;i<FV_CREDENTIAL_ENVELOPE_SIZE;i++)h.wrapped_vmk[i]=(uint8_t)(0x80u+i);return h;}
static fv_device_secret_t roots(void){fv_device_secret_t r;for(size_t i=0;i<sizeof(r.device_secret);i++)r.device_secret[i]=(uint8_t)i;return r;}
static void retag(uint8_t record[256],const fv_device_secret_t*r){static const uint8_t domain[]="fuse-vault/v1/vault-header/auth";uint8_t key[32];CHECK(fv_hmac_sha256(r->device_secret,sizeof(r->device_secret),domain,sizeof(domain)-1u,NULL,0u,key));CHECK(fv_hmac_sha256(key,sizeof(key),record,224u,NULL,0u,record+224u));memset(key,0,sizeof(key));}

static void test_golden_and_rejections(void){fv_device_secret_t r=roots();fv_vault_header_t h=header(0x0807060504030201ull),out;uint8_t v[256];CHECK(fv_vault_header_serialize(&h,&r,v)==FV_VAULT_HEADER_STORE_OK);
 static const uint8_t prefix[]={ 'F','V','H','D','R','0','1',0,1,0,1,0,0,1,0,0,1,2,3,4,5,6,7,8,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,2,0,0,0,1,0,0,0,4,3,2,0,8,7,6,0};CHECK(memcmp(v,prefix,sizeof(prefix))==0);
 static const uint8_t golden_tag[32]={0xa1,0x5f,0x13,0xf5,0xef,0x76,0xdb,0x0a,0x8f,0xec,0x32,0x76,0x91,0x44,0x2c,0x7e,0x5f,0xe0,0x91,0xbd,0x95,0xf0,0xa3,0x23,0xa6,0x6e,0x99,0xc6,0x65,0x52,0x6a,0x6c};
 CHECK(memcmp(v+224,golden_tag,32)==0);
 CHECK(fv_vault_header_parse(v,&r,h.vault_id,&out)==FV_VAULT_HEADER_STORE_OK);CHECK(memcmp(&h,&out,sizeof(h))==0);
 uint8_t bad[256];memcpy(bad,v,256);bad[8]=2;retag(bad,&r);CHECK(fv_vault_header_parse(bad,&r,NULL,&out)==FV_VAULT_HEADER_STORE_INVALID);CHECK(out.sequence==0u);
 memcpy(bad,v,256);bad[40]=99;retag(bad,&r);CHECK(fv_vault_header_parse(bad,&r,NULL,&out)==FV_VAULT_HEADER_STORE_INVALID);
 memcpy(bad,v,256);bad[44]=99;retag(bad,&r);CHECK(fv_vault_header_parse(bad,&r,NULL,&out)==FV_VAULT_HEADER_STORE_INVALID);
 memcpy(bad,v,256);bad[88]=1;bad[89]=0;retag(bad,&r);CHECK(fv_vault_header_parse(bad,&r,NULL,&out)==FV_VAULT_HEADER_STORE_INVALID);
 memcpy(bad,v,256);bad[100]^=1;CHECK(fv_vault_header_parse(bad,&r,NULL,&out)==FV_VAULT_HEADER_STORE_INVALID);uint8_t wrong[16];memset(wrong,7,16);CHECK(fv_vault_header_parse(v,&r,wrong,&out)==FV_VAULT_HEADER_STORE_INVALID);
}
static void test_every_cut(void){fv_device_secret_t r=roots();fv_vault_header_t old=header(1),next=header(2),loaded;memory_t baseline={.cut=512};fv_block_device_t d={&OPS,&baseline};CHECK(fv_vault_header_store_update(&d,&r,&old)==FV_VAULT_HEADER_STORE_OK);for(size_t cut=0;cut<=512;cut++){memory_t m=baseline;m.cut=cut;fv_block_device_t x={&OPS,&m};fv_vault_header_store_result_t u=fv_vault_header_store_update(&x,&r,&next);CHECK((u==FV_VAULT_HEADER_STORE_OK)==(cut==512));CHECK(fv_vault_header_store_load(&x,&r,old.vault_id,&loaded)==FV_VAULT_HEADER_STORE_OK);CHECK(loaded.sequence==(cut>=256?2u:1u));}
 memory_t m=baseline;m.fail_sync=true;fv_block_device_t x={&OPS,&m};CHECK(fv_vault_header_store_update(&x,&r,&next)==FV_VAULT_HEADER_STORE_IO_ERROR);m.fail_sync=false;CHECK(fv_vault_header_store_load(&x,&r,old.vault_id,&loaded)==FV_VAULT_HEADER_STORE_OK);CHECK(loaded.sequence==2u);m.short_read=true;CHECK(fv_vault_header_store_load(&x,&r,NULL,&loaded)==FV_VAULT_HEADER_STORE_IO_ERROR);CHECK(loaded.sequence==0u);uint8_t b[512];CHECK(rd(&x,2,1,b)==FV_BLOCK_ERROR_OUT_OF_RANGE);
}
static void test_ambiguous_sequences(void){fv_device_secret_t r=roots();memory_t m={.cut=512};fv_block_device_t d={&OPS,&m};fv_vault_header_t a=header(1),b=header(1),out;CHECK(fv_vault_header_serialize(&a,&r,m.bytes)==FV_VAULT_HEADER_STORE_OK);CHECK(fv_vault_header_serialize(&b,&r,m.bytes+512)==FV_VAULT_HEADER_STORE_OK);CHECK(fv_vault_header_store_load(&d,&r,a.vault_id,&out)==FV_VAULT_HEADER_STORE_INVALID);CHECK(out.sequence==0u);b=header(UINT64_MAX);CHECK(fv_vault_header_serialize(&b,&r,m.bytes+512)==FV_VAULT_HEADER_STORE_OK);CHECK(fv_vault_header_store_load(&d,&r,a.vault_id,&out)==FV_VAULT_HEADER_STORE_INVALID);}
int main(void){test_golden_and_rejections();test_every_cut();test_ambiguous_sequences();puts("All vault-header store tests passed.");return EXIT_SUCCESS;}
