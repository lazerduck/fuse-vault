#define _POSIX_C_SOURCE 200809L
#include "fuse_vault/encrypted_block.h"
#include "fuse_vault/journal_authenticator.h"
#include "file_block_device.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RAW_BLOCKS 16u
#define RAW_SIZE (RAW_BLOCKS * FV_BLOCK_SIZE)
typedef struct { uint8_t bytes[RAW_SIZE]; uint64_t blocks; size_t cut; bool fail_sync; } memory_t;

static bool range(memory_t*c,uint64_t f,uint32_t n){return n>0u&&f<c->blocks&&(uint64_t)n<=c->blocks-f;}
static fv_block_result_t rd(fv_block_device_t*d,uint64_t f,uint32_t n,uint8_t*out){memory_t*c=d->context;if(!range(c,f,n))return FV_BLOCK_ERROR_OUT_OF_RANGE;memcpy(out,c->bytes+(size_t)f*512u,(size_t)n*512u);return FV_BLOCK_OK;}
static fv_block_result_t wr(fv_block_device_t*d,uint64_t f,uint32_t n,const uint8_t*in){memory_t*c=d->context;if(!range(c,f,n))return FV_BLOCK_ERROR_OUT_OF_RANGE;size_t length=(size_t)n*512u,amount=c->cut<length?c->cut:length;memcpy(c->bytes+(size_t)f*512u,in,amount);return amount==length?FV_BLOCK_OK:FV_BLOCK_ERROR_IO;}
static fv_block_result_t sy(fv_block_device_t*d){memory_t*c=d->context;return c->fail_sync?FV_BLOCK_ERROR_IO:FV_BLOCK_OK;}
static uint64_t bc(const fv_block_device_t*d){const memory_t*c=d->context;return c->blocks;}
static bool pr(const fv_block_device_t*d){const memory_t*c=d->context;return c->blocks==RAW_BLOCKS;}
static const fv_block_device_ops_t mem_ops={rd,wr,sy,bc,pr};
static void device(memory_t*m,fv_block_device_t*d){memset(m,0,sizeof(*m));m->blocks=RAW_BLOCKS;m->cut=(size_t)-1;d->ops=&mem_ops;d->context=m;}
static bool rng(void*context,uint8_t*out,size_t n){uint8_t *v=context;for(size_t i=0u;i<n;++i)out[i]=(*v)++;return true;}
static void fill(uint8_t*p,size_t n,uint32_t seed){for(size_t i=0u;i<n;++i){seed=seed*1664525u+1013904223u;p[i]=(uint8_t)(seed>>24u);}}
static void setup(fv_encrypted_block_t*e,fv_block_device_t*raw,const fv_volume_master_key_t*k,const uint8_t id[16],uint8_t start){fv_encryption_stack_descriptor_t stack;fv_crypto_stack_default(&stack);assert(fv_encrypted_block_init(e,raw,k,id,&stack,rng,&start));}
static void hex(const uint8_t*p,size_t n){for(size_t i=0u;i<n;++i)printf("%02x",p[i]);puts("");}

static void golden(void){
    memory_t m;fv_block_device_t raw;device(&m,&raw);fv_volume_master_key_t key;uint8_t id[16];
    for(size_t i=0u;i<32u;++i)key.bytes[i]=(uint8_t)i;
    for(size_t i=0u;i<16u;++i)id[i]=(uint8_t)(0xa0u+i);
    fv_encrypted_block_t e;setup(&e,&raw,&key,id,1u);uint8_t plain[512];for(size_t i=0u;i<512u;++i)plain[i]=(uint8_t)i;
    assert(e.interface.ops->write(&e.interface,2u,1u,plain)==FV_BLOCK_OK);
    uint8_t digest[32];uint8_t digest_key[32]={0};static const uint8_t custom[]="golden-record";
    assert(fv_kmac256(digest_key,sizeof(digest_key),m.bytes+8u*512u,616u,
                      custom,sizeof(custom)-1u,digest,sizeof(digest)));
    static const uint8_t expected_key[16]={0xc7,0x19,0x35,0x73,0xf3,0xc2,0x6b,0xf5,0x30,0x3c,0x8b,0x76,0x9e,0x54,0x8c,0x84};
    static const uint8_t expected_nonce_key[32]={0x0c,0x4e,0x60,0x08,0xfa,0xf4,0x68,0x98,0x6d,0xdc,0x03,0xd7,0x4a,0x21,0xc9,0x93,0x89,0x0f,0x16,0x14,0x2e,0x42,0x42,0x22,0x6e,0x88,0xe2,0x69,0x57,0xb9,0x7d,0x99};
    static const uint8_t expected_nonce[16]={0x1e,0x6e,0x3f,0x79,0xd7,0xd8,0x60,0x3d,0xb2,0xe5,0x46,0x25,0xfc,0x95,0x76,0x5a};
    static const uint8_t expected_digest[32]={0xea,0x40,0xb3,0x08,0xc2,0x02,0x68,0x96,0xc9,0xe8,0xd9,0xed,0xcc,0xb3,0xe2,0xb0,0xac,0x00,0xd3,0xb0,0x25,0x71,0xf2,0xcb,0x5a,0x94,0x69,0x4a,0x4e,0x1a,0x60,0x79};
    if(memcmp(e.encryption_key,expected_key,16u)||memcmp(e.nonce_key,expected_nonce_key,32u)||memcmp(m.bytes+8u*512u+56u,expected_nonce,16u)||memcmp(digest,expected_digest,32u)){
        puts("golden values:");hex(e.encryption_key,16u);hex(e.nonce_key,32u);hex(m.bytes+8u*512u+56u,16u);hex(digest,32u);assert(false);
    }
    fv_encrypted_block_lock(&e);fv_volume_master_key_clear(&key);
}

static void round_trip_and_tamper(void){
    memory_t m;fv_block_device_t raw;device(&m,&raw);fv_volume_master_key_t key;fill(key.bytes,32u,3u);uint8_t id[16],plain[512],out[512];fill(id,16u,4u);fill(plain,512u,5u);
    fv_encrypted_block_t e;setup(&e,&raw,&key,id,9u);assert(e.interface.ops->block_count(&e.interface)==4u);
    assert(e.interface.ops->write(&e.interface,0u,1u,plain)==FV_BLOCK_OK);memset(out,0,512u);assert(e.interface.ops->read(&e.interface,0u,1u,out)==FV_BLOCK_OK);assert(memcmp(out,plain,512u)==0);
    uint8_t saved[RAW_SIZE];memcpy(saved,m.bytes,sizeof(saved));
    const size_t flips[]={0u,16u,56u,72u,88u,599u,600u,615u,616u};
    for(size_t i=0u;i<sizeof(flips)/sizeof(flips[0]);++i){memcpy(m.bytes,saved,sizeof(saved));m.bytes[flips[i]]^=1u;memset(out,0xa5,512u);assert(e.interface.ops->read(&e.interface,0u,1u,out)==FV_BLOCK_ERROR_INTEGRITY);for(size_t j=0u;j<512u;++j)assert(out[j]==0u);}
    memcpy(m.bytes,saved,sizeof(saved));memcpy(m.bytes+4u*512u,m.bytes,1024u);memset(out,0xa5,512u);assert(e.interface.ops->read(&e.interface,1u,1u,out)==FV_BLOCK_ERROR_INTEGRITY);
    memcpy(m.bytes,saved,sizeof(saved));uint8_t other_id[16];memcpy(other_id,id,16u);other_id[0]^=1u;fv_encrypted_block_t other;setup(&other,&raw,&key,other_id,40u);assert(other.interface.ops->read(&other.interface,0u,1u,out)==FV_BLOCK_ERROR_INTEGRITY);fv_encrypted_block_lock(&other);
    memcpy(m.bytes,saved,sizeof(saved));m.blocks=1u;assert(e.interface.ops->read(&e.interface,0u,1u,out)==FV_BLOCK_ERROR_IO);m.blocks=RAW_BLOCKS;
    fv_encrypted_block_fault(&e);assert(!e.ready);assert(e.untrusted==NULL);
    for(size_t i=0u;i<sizeof(e.encryption_key);++i)assert(e.encryption_key[i]==0u);
    for(size_t i=0u;i<sizeof(e.nonce_key);++i)assert(e.nonce_key[i]==0u);
    for(size_t i=0u;i<sizeof(e.epoch);++i)assert(e.epoch[i]==0u);
    memset(out,0xa5,512u);assert(e.interface.ops->read(&e.interface,0u,1u,out)==FV_BLOCK_ERROR_NOT_READY);
    assert(e.interface.ops->write(&e.interface,0u,1u,plain)==FV_BLOCK_ERROR_NOT_READY);
}

static void cut_points(void){
    memory_t m;fv_block_device_t raw;device(&m,&raw);fv_volume_master_key_t key;uint8_t id[16],old[512],next[512],out[512];fill(key.bytes,32u,10u);fill(id,16u,11u);fill(old,512u,12u);fill(next,512u,13u);
    fv_encrypted_block_t e;setup(&e,&raw,&key,id,1u);assert(e.interface.ops->write(&e.interface,0u,1u,old)==FV_BLOCK_OK);uint8_t baseline[RAW_SIZE];memcpy(baseline,m.bytes,sizeof(baseline));fv_encrypted_block_lock(&e);
    for(size_t cut=0u;cut<=1024u;++cut){memcpy(m.bytes,baseline,sizeof(baseline));m.cut=cut;setup(&e,&raw,&key,id,(uint8_t)(20u+(cut%200u)));fv_block_result_t w=e.interface.ops->write(&e.interface,0u,1u,next);assert((cut==1024u&&w==FV_BLOCK_OK)||(cut<1024u&&w==FV_BLOCK_ERROR_IO));fv_encrypted_block_lock(&e);m.cut=(size_t)-1;setup(&e,&raw,&key,id,(uint8_t)(221u+(cut%30u)));assert(e.interface.ops->read(&e.interface,0u,1u,out)==FV_BLOCK_OK);assert(memcmp(out,old,512u)==0||memcmp(out,next,512u)==0);fv_encrypted_block_lock(&e);}
}

static void randomized_model(void){
    memory_t m;fv_block_device_t raw;device(&m,&raw);fv_volume_master_key_t key;uint8_t id[16],model[4][512],value[512],out[512];fill(key.bytes,32u,90u);fill(id,16u,91u);memset(model,0,sizeof(model));fv_encrypted_block_t e;setup(&e,&raw,&key,id,60u);uint32_t state=1u;
    for(unsigned step=0u;step<1000u;++step){state=state*1103515245u+12345u;uint64_t block=(state>>16u)%4u;if((state&3u)!=0u){fill(value,512u,state);assert(e.interface.ops->write(&e.interface,block,1u,value)==FV_BLOCK_OK);memcpy(model[block],value,512u);}else{memset(out,0xa5,512u);assert(e.interface.ops->read(&e.interface,block,1u,out)==FV_BLOCK_OK);assert(memcmp(out,model[block],512u)==0);}if(step%97u==0u){fv_encrypted_block_lock(&e);setup(&e,&raw,&key,id,(uint8_t)(61u+step/97u));}}
    fv_encrypted_block_lock(&e);assert(!e.ready);assert(e.interface.ops->read(&e.interface,0u,1u,out)==FV_BLOCK_ERROR_NOT_READY);
}

static void file_backed(void){
    char path[]="/tmp/fuse-vault-data-XXXXXX";int fd=mkstemp(path);assert(fd>=0);assert(close(fd)==0);
    fv_host_file_block_context_t context;fv_block_device_t raw;assert(fv_host_file_block_device_init(&raw,&context,path,RAW_BLOCKS));
    fv_volume_master_key_t key;uint8_t id[16],plain[512],out[512];fill(key.bytes,32u,110u);fill(id,16u,111u);fill(plain,512u,112u);
    fv_encrypted_block_t e;setup(&e,&raw,&key,id,90u);assert(e.interface.ops->write(&e.interface,3u,1u,plain)==FV_BLOCK_OK);fv_encrypted_block_lock(&e);
    setup(&e,&raw,&key,id,120u);assert(e.interface.ops->read(&e.interface,3u,1u,out)==FV_BLOCK_OK);assert(memcmp(out,plain,512u)==0);fv_encrypted_block_lock(&e);assert(unlink(path)==0);
}

int main(void){golden();round_trip_and_tamper();cut_points();randomized_model();file_backed();puts("encrypted block tests passed");return 0;}
