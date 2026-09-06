#include "fuse_vault/encrypted_block.h"

#include "fuse_vault/journal_authenticator.h"
#include "crypto_aead.h"

#include <limits.h>
#include <string.h>

#define RECORD_BLOCKS 2u
#define RECORD_SIZE (RECORD_BLOCKS * FV_BLOCK_SIZE)
#define HEADER_SIZE 88u
#define CIPHERTEXT_OFFSET HEADER_SIZE
#define TAG_OFFSET (CIPHERTEXT_OFFSET + FV_BLOCK_SIZE)
#define RECORD_USED (TAG_OFFSET + 16u)

static const uint8_t magic[8] = {'F','V','D','A','T','A','1',0};
static const uint8_t encryption_domain[] =
    "fuse-vault/v1/data/ascon-aead128/key";
static const uint8_t nonce_domain[] =
    "fuse-vault/v1/data/ascon-aead128/nonce-key";
static const uint8_t nonce_customization[] =
    "fuse-vault/v1/data/ascon-aead128/nonce";

static void clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}
static void put16(uint8_t *p, uint16_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8u);
}
static void put64(uint8_t *p, uint64_t v) {
    for (unsigned i=0u;i<8u;++i) p[i]=(uint8_t)(v>>(i*8u));
}
static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8u));
}
static uint64_t get64(const uint8_t *p) {
    uint64_t v=0u; for(unsigned i=0u;i<8u;++i)v|=(uint64_t)p[i]<<(i*8u);return v;
}
static bool equal(const uint8_t *a,const uint8_t *b,size_t n) {
    uint8_t d=0u;for(size_t i=0u;i<n;++i)d|=(uint8_t)(a[i]^b[i]);return d==0u;
}
static bool zero(const uint8_t *p,size_t n) {
    uint8_t v=0u;for(size_t i=0u;i<n;++i)v|=p[i];return v==0u;
}
static bool erased(const uint8_t *p,size_t n) {
    bool z=true,f=true;for(size_t i=0u;i<n;++i){z=z&&p[i]==0u;f=f&&p[i]==0xffu;}return z||f;
}

static bool derive(fv_encrypted_block_t *e,const fv_volume_master_key_t *vmk) {
    uint8_t full[32];
    bool ok=fv_hmac_sha256(vmk->bytes,sizeof(vmk->bytes),encryption_domain,
                           sizeof(encryption_domain)-1u,e->vault_id,
                           sizeof(e->vault_id),full);
    if(ok)memcpy(e->encryption_key,full,sizeof(e->encryption_key));
    if(ok)ok=fv_kmac256(vmk->bytes,sizeof(vmk->bytes),e->vault_id,
                        sizeof(e->vault_id),nonce_domain,
                        sizeof(nonce_domain)-1u,e->nonce_key,
                        sizeof(e->nonce_key));
    clear(full,sizeof(full));return ok;
}

static bool make_nonce(const fv_encrypted_block_t *e,uint64_t logical,
                       uint64_t generation,const uint8_t epoch[16],
                       uint64_t counter,uint8_t out[16]) {
    uint8_t message[48];
    memcpy(message,e->vault_id,16u);put64(message+16u,logical);
    put64(message+24u,generation);memcpy(message+32u,epoch,16u);
    /* The per-session counter is mixed by changing the final epoch bytes. */
    for(unsigned i=0u;i<8u;++i)message[40u+i]^=(uint8_t)(counter>>(i*8u));
    bool ok=fv_kmac256(e->nonce_key,sizeof(e->nonce_key),message,sizeof(message),
                       nonce_customization,sizeof(nonce_customization)-1u,out,16u);
    clear(message,sizeof(message));return ok;
}

typedef enum { SLOT_EMPTY, SLOT_VALID, SLOT_INVALID, SLOT_IO } slot_state_t;
typedef struct { uint64_t generation; uint64_t counter; uint8_t plain[512]; } decoded_t;

static slot_state_t decode(fv_encrypted_block_t *e,uint64_t logical,unsigned slot,
                           decoded_t *decoded) {
    uint8_t record[RECORD_SIZE];
    uint64_t physical=logical*4u+(uint64_t)slot*2u;
    fv_block_result_t r=e->untrusted->ops->read(e->untrusted,physical,2u,record);
    if(r!=FV_BLOCK_OK){clear(record,sizeof(record));return SLOT_IO;}
    if(erased(record,sizeof(record))){clear(record,sizeof(record));return SLOT_EMPTY;}
    bool canonical=memcmp(record,magic,8u)==0&&get16(record+8u)==1u&&
        get16(record+10u)==HEADER_SIZE&&get16(record+12u)==RECORD_USED&&
        get16(record+14u)==0u&&get64(record+16u)==logical&&
        get64(record+24u)>0u&&!zero(record+32u,16u)&&get64(record+48u)>0u&&
        equal(record+72u,e->vault_id,16u)&&zero(record+RECORD_USED,
                                                RECORD_SIZE-RECORD_USED);
    uint8_t expected_nonce[16];
    if(canonical)canonical=make_nonce(e,logical,get64(record+24u),record+32u,
                                      get64(record+48u),expected_nonce)&&
                            equal(expected_nonce,record+56u,16u);
    unsigned long long length=0u;
    int auth=-1;
    if(canonical)auth=crypto_aead_decrypt(decoded->plain,&length,NULL,
        record+CIPHERTEXT_OFFSET,FV_BLOCK_SIZE+16u,record,HEADER_SIZE,
        record+56u,e->encryption_key);
    clear(expected_nonce,sizeof(expected_nonce));
    if(auth==0&&length==FV_BLOCK_SIZE&&fv_crypto_pipeline_decrypt_block(
        &e->pipeline,logical,get64(record+24u),record+32u,get64(record+48u),
        decoded->plain)){decoded->generation=get64(record+24u);
        decoded->counter=get64(record+48u);clear(record,sizeof(record));return SLOT_VALID;}
    clear(decoded,sizeof(*decoded));clear(record,sizeof(record));return SLOT_INVALID;
}

static fv_block_result_t select(fv_encrypted_block_t *e,uint64_t logical,
                                decoded_t *selected,bool *found) {
    decoded_t a,b;slot_state_t sa=decode(e,logical,0u,&a),sb=decode(e,logical,1u,&b);
    *found=false;
    if(sa==SLOT_IO||sb==SLOT_IO){clear(&a,sizeof(a));clear(&b,sizeof(b));return FV_BLOCK_ERROR_IO;}
    if(sa==SLOT_VALID&&sb==SLOT_VALID) {
        uint64_t hi=a.generation>b.generation?a.generation:b.generation;
        uint64_t lo=a.generation>b.generation?b.generation:a.generation;
        if(hi-lo!=1u){clear(&a,sizeof(a));clear(&b,sizeof(b));return FV_BLOCK_ERROR_INTEGRITY;}
        *selected=a.generation>b.generation?a:b;*found=true;
    } else if(sa==SLOT_VALID){*selected=a;*found=true;}
    else if(sb==SLOT_VALID){*selected=b;*found=true;}
    else if(sa==SLOT_INVALID||sb==SLOT_INVALID){clear(&a,sizeof(a));clear(&b,sizeof(b));return FV_BLOCK_ERROR_INTEGRITY;}
    clear(&a,sizeof(a));clear(&b,sizeof(b));return FV_BLOCK_OK;
}

static fv_block_result_t read_blocks(fv_block_device_t *device,uint64_t first,
                                     uint32_t count,uint8_t *output) {
    fv_encrypted_block_t *e=device?device->context:NULL;
    if(!e||!output||count!=1u)return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    if(!e->ready)return FV_BLOCK_ERROR_NOT_READY;
    if(first>=e->logical_blocks)return FV_BLOCK_ERROR_OUT_OF_RANGE;
    decoded_t d;bool found=false;fv_block_result_t r=select(e,first,&d,&found);
    if(r==FV_BLOCK_OK){if(found)memcpy(output,d.plain,512u);else memset(output,0,512u);}
    else clear(output,512u);
    clear(&d,sizeof(d));return r;
}

static fv_block_result_t write_blocks(fv_block_device_t *device,uint64_t first,
                                      uint32_t count,const uint8_t *input) {
    fv_encrypted_block_t *e=device?device->context:NULL;
    if(!e||!input||count!=1u)return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    if(!e->ready)return FV_BLOCK_ERROR_NOT_READY;
    if(first>=e->logical_blocks)return FV_BLOCK_ERROR_OUT_OF_RANGE;
    decoded_t old;bool found=false;fv_block_result_t result=select(e,first,&old,&found);
    if(result!=FV_BLOCK_OK){clear(&old,sizeof(old));return result;}
    if((found&&old.generation==UINT64_MAX)||e->next_counter==UINT64_MAX){clear(&old,sizeof(old));return FV_BLOCK_ERROR_IO;}
    uint64_t generation=found?old.generation+1u:1u;
    uint64_t counter=e->next_counter++;
    unsigned slot=(unsigned)((generation-1u)&1u);
    uint8_t record[RECORD_SIZE];memset(record,0,sizeof(record));memcpy(record,magic,8u);
    put16(record+8u,1u);put16(record+10u,HEADER_SIZE);put16(record+12u,RECORD_USED);
    put64(record+16u,first);put64(record+24u,generation);memcpy(record+32u,e->epoch,16u);
    put64(record+48u,counter);memcpy(record+72u,e->vault_id,16u);
    if(!make_nonce(e,first,generation,e->epoch,counter,record+56u)){result=FV_BLOCK_ERROR_IO;goto done;}
    uint8_t layered[512u];memcpy(layered,input,sizeof(layered));
    if(!fv_crypto_pipeline_encrypt_block(&e->pipeline,first,generation,e->epoch,
                                         counter,layered)){clear(layered,sizeof(layered));result=FV_BLOCK_ERROR_IO;goto done;}
    unsigned long long length=0u;
    if(crypto_aead_encrypt(record+CIPHERTEXT_OFFSET,&length,layered,512u,record,
        HEADER_SIZE,NULL,record+56u,e->encryption_key)!=0||length!=528u){clear(layered,sizeof(layered));result=FV_BLOCK_ERROR_IO;goto done;}
    clear(layered,sizeof(layered));
    result=e->untrusted->ops->write(e->untrusted,first*4u+(uint64_t)slot*2u,2u,record);
    if(result==FV_BLOCK_OK)result=e->untrusted->ops->sync(e->untrusted);
    if(result!=FV_BLOCK_OK){result=FV_BLOCK_ERROR_IO;goto done;}
    decoded_t check;slot_state_t state=decode(e,first,slot,&check);
    if(state!=SLOT_VALID||check.generation!=generation||memcmp(check.plain,input,512u)!=0)
        result=state==SLOT_IO?FV_BLOCK_ERROR_IO:FV_BLOCK_ERROR_INTEGRITY;
    clear(&check,sizeof(check));
done:
    clear(&old,sizeof(old));clear(record,sizeof(record));return result;
}
static fv_block_result_t sync_blocks(fv_block_device_t *device) {
    fv_encrypted_block_t *e=device?device->context:NULL;
    if(!e)return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    if(!e->ready)return FV_BLOCK_ERROR_NOT_READY;
    return e->untrusted->ops->sync(e->untrusted);
}
static uint64_t count_blocks(const fv_block_device_t *device) {
    const fv_encrypted_block_t *e=device?device->context:NULL;return e&&e->ready?e->logical_blocks:0u;
}
static bool present(const fv_block_device_t *device) {
    const fv_encrypted_block_t *e=device?device->context:NULL;
    return e&&e->ready&&e->untrusted->ops->is_present(e->untrusted);
}
static const fv_block_device_ops_t ops={read_blocks,write_blocks,sync_blocks,count_blocks,present};

bool fv_encrypted_block_init(fv_encrypted_block_t *e,fv_block_device_t *u,
 const fv_volume_master_key_t *vmk,const uint8_t vault_id[16],
 const fv_encryption_stack_descriptor_t *stack,
 fv_encrypted_block_random_fill_fn random_fill,void *random_context) {
    if(!e)return false;
    clear(e,sizeof(*e));
    if(!u||!vmk||!vault_id||!stack||!random_fill||!u->ops||!u->ops->read||!u->ops->write||
       !u->ops->sync||!u->ops->block_count||!u->ops->is_present||!u->ops->is_present(u))return false;
    uint64_t blocks=u->ops->block_count(u);if(blocks<4u||blocks%4u!=0u||zero(vault_id,16u))return false;
    e->untrusted=u;memcpy(e->vault_id,vault_id,16u);e->logical_blocks=blocks/4u;
    if(!random_fill(random_context,e->epoch,16u)||zero(e->epoch,16u)||!derive(e,vmk)||
       !fv_crypto_pipeline_init(&e->pipeline,stack,vmk,vault_id)){clear(e,sizeof(*e));return false;}
    e->next_counter=1u;e->interface.ops=&ops;e->interface.context=e;e->ready=true;return true;
}
void fv_encrypted_block_lock(fv_encrypted_block_t *e){
    if(e){clear(e,sizeof(*e));e->interface.ops=&ops;e->interface.context=e;}
}
void fv_encrypted_block_fault(fv_encrypted_block_t *e){fv_encrypted_block_lock(e);}
