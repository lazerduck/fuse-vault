#include "fuse_vault/vault_header_store.h"

#include "fuse_vault/credential_envelope.h"
#include "fuse_vault/journal_authenticator.h"

#include <limits.h>
#include <string.h>

#define TAG_OFFSET 224u
#define STACK_OFFSET 184u
static const uint8_t MAGIC[8] = {'F','V','H','D','R','0','2',0};
static const uint8_t DOMAIN[] = "fuse-vault/v1/vault-header/auth";

static void clear(void *p, size_t n) { volatile uint8_t *b=p; while(n--) *b++=0; }
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(v>>(i*8u));}
static void put64(uint8_t *p,uint64_t v){put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32u));}
static uint16_t get16(const uint8_t*p){return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8u));}
static uint32_t get32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);}
static uint64_t get64(const uint8_t*p){return (uint64_t)get32(p)|((uint64_t)get32(p+4)<<32u);}
static bool equal(const uint8_t*a,const uint8_t*b,size_t n){uint8_t d=0;for(size_t i=0;i<n;i++)d|=(uint8_t)(a[i]^b[i]);return d==0;}

static bool tag(const fv_device_secret_t *roots,const uint8_t *record,uint8_t out[32]) {
    uint8_t key[32];
    bool ok=fv_hmac_sha256(roots->device_secret,FV_DEVICE_SECRET_SIZE,DOMAIN,sizeof(DOMAIN)-1u,NULL,0u,key);
    if(ok) ok=fv_hmac_sha256(key,sizeof(key),record,TAG_OFFSET,NULL,0u,out);
    clear(key,sizeof(key)); return ok;
}

fv_vault_header_store_result_t fv_vault_header_serialize(const fv_vault_header_t*h,const fv_device_secret_t*r,uint8_t o[FV_VAULT_HEADER_RECORD_SIZE]){
    if(!h||!r||!o||!fv_vault_header_valid(h)) { if(o) clear(o,FV_VAULT_HEADER_RECORD_SIZE); return FV_VAULT_HEADER_STORE_INVALID; }
    memset(o,0,FV_VAULT_HEADER_RECORD_SIZE); memcpy(o,MAGIC,8); put16(o+8,FV_VAULT_HEADER_FORMAT_VERSION); put16(o+10,FV_VAULT_HEADER_ENCODING_VERSION);
    put16(o+12,FV_VAULT_HEADER_RECORD_SIZE); put64(o+16,h->sequence); memcpy(o+24,h->vault_id,16); put32(o+40,(uint32_t)h->entry_method); put32(o+44,(uint32_t)h->crypto_profile);
    put32(o+48,h->branch_a_cost); put32(o+52,h->branch_b_cost); memcpy(o+56,h->branch_a_salt,16); memcpy(o+72,h->branch_b_salt,16); put16(o+88,h->wrapped_vmk_length); memcpy(o+92,h->wrapped_vmk,h->wrapped_vmk_length);
    put16(o+STACK_OFFSET,h->encryption_stack.format_version); o[STACK_OFFSET+2u]=h->encryption_stack.layer_count; o[STACK_OFFSET+3u]=h->encryption_stack.reserved;
    for(size_t x=0u;x<FV_ENCRYPTION_STACK_MAX_LAYERS;x++){put16(o+STACK_OFFSET+4u+x*4u,h->encryption_stack.layers[x].algorithm_id);put16(o+STACK_OFFSET+6u+x*4u,h->encryption_stack.layers[x].algorithm_version);}
    if(!tag(r,o,o+TAG_OFFSET)){clear(o,FV_VAULT_HEADER_RECORD_SIZE);return FV_VAULT_HEADER_STORE_IO_ERROR;} return FV_VAULT_HEADER_STORE_OK;
}

fv_vault_header_store_result_t fv_vault_header_parse(const uint8_t i[FV_VAULT_HEADER_RECORD_SIZE],const fv_device_secret_t*r,const uint8_t expected[16],fv_vault_header_t*h){
    if (h) clear(h, sizeof(*h));
    if (!i || !r || !h) return FV_VAULT_HEADER_STORE_INVALID;
    uint8_t t[32]; bool auth=tag(r,i,t)&&equal(t,i+TAG_OFFSET,32); clear(t,sizeof(t));
    if(!auth||memcmp(i,MAGIC,8)!=0||get16(i+8)!=FV_VAULT_HEADER_FORMAT_VERSION||get16(i+10)!=FV_VAULT_HEADER_ENCODING_VERSION||get16(i+12)!=FV_VAULT_HEADER_RECORD_SIZE||get16(i+14)!=0u||get16(i+90)!=0u) return FV_VAULT_HEADER_STORE_INVALID;
    /* Unused wrapped-key capacity and bytes 204..223 are canonical zero. */
    for(size_t x=92u+FV_CREDENTIAL_ENVELOPE_SIZE;x<STACK_OFFSET;x++)if(i[x]!=0u)return FV_VAULT_HEADER_STORE_INVALID;
    for(size_t x=STACK_OFFSET+20u;x<TAG_OFFSET;x++)if(i[x]!=0u)return FV_VAULT_HEADER_STORE_INVALID;
    h->sequence=get64(i+16); memcpy(h->vault_id,i+24,16); h->entry_method=(fv_secret_method_t)get32(i+40); h->crypto_profile=(fv_crypto_profile_t)get32(i+44); h->branch_a_cost=get32(i+48); h->branch_b_cost=get32(i+52); memcpy(h->branch_a_salt,i+56,16); memcpy(h->branch_b_salt,i+72,16); h->wrapped_vmk_length=get16(i+88); if(h->wrapped_vmk_length<=FV_WRAPPED_VMK_CAPACITY)memcpy(h->wrapped_vmk,i+92,h->wrapped_vmk_length);
    h->encryption_stack.format_version=get16(i+STACK_OFFSET);h->encryption_stack.layer_count=i[STACK_OFFSET+2u];h->encryption_stack.reserved=i[STACK_OFFSET+3u];for(size_t x=0u;x<FV_ENCRYPTION_STACK_MAX_LAYERS;x++){h->encryption_stack.layers[x].algorithm_id=get16(i+STACK_OFFSET+4u+x*4u);h->encryption_stack.layers[x].algorithm_version=get16(i+STACK_OFFSET+6u+x*4u);}
    if(!fv_vault_header_valid(h)||(expected&&!equal(expected,h->vault_id,16))){clear(h,sizeof(*h));return FV_VAULT_HEADER_STORE_INVALID;} return FV_VAULT_HEADER_STORE_OK;
}

static bool usable(fv_block_device_t*d){return d&&d->ops&&d->ops->read&&d->ops->write&&d->ops->sync&&d->ops->block_count&&d->ops->is_present&&d->ops->is_present(d)&&d->ops->block_count(d)>=2u;}
fv_vault_header_store_result_t fv_vault_header_store_load(fv_block_device_t*d,const fv_device_secret_t*r,const uint8_t expected[16],fv_vault_header_t*h){
    if (h) clear(h, sizeof(*h));
    if (!usable(d) || !r || !h) return FV_VAULT_HEADER_STORE_INVALID;
    bool found=false,invalid=false; uint64_t seq=0; uint8_t block[512]; fv_vault_header_t candidate;
    for(uint64_t s=0;s<2u;s++){if(d->ops->read(d,s,1u,block)!=FV_BLOCK_OK){clear(block,sizeof(block));return FV_VAULT_HEADER_STORE_IO_ERROR;} bool all_zero=true,all_ff=true;for(size_t j=0;j<sizeof(block);j++){all_zero&=block[j]==0u;all_ff&=block[j]==0xffu;}bool erased=all_zero||all_ff;
      fv_vault_header_store_result_t x=fv_vault_header_parse(block,r,expected,&candidate); if(x==FV_VAULT_HEADER_STORE_OK){if(found){uint64_t hi=candidate.sequence>seq?candidate.sequence:seq,lo=candidate.sequence>seq?seq:candidate.sequence;if(hi-lo!=1u){clear(block,sizeof(block));clear(&candidate,sizeof(candidate));clear(h,sizeof(*h));return FV_VAULT_HEADER_STORE_INVALID;}if(candidate.sequence<seq)continue;} *h=candidate;seq=candidate.sequence;found=true;} else if(!erased) invalid=true; }
    clear(block,sizeof(block));clear(&candidate,sizeof(candidate));return found?FV_VAULT_HEADER_STORE_OK:(invalid?FV_VAULT_HEADER_STORE_INVALID:FV_VAULT_HEADER_STORE_NOT_FOUND);
}
fv_vault_header_store_result_t fv_vault_header_store_update(fv_block_device_t*d,const fv_device_secret_t*r,const fv_vault_header_t*h){
    if (!usable(d) || !r || !h) return FV_VAULT_HEADER_STORE_INVALID;
    fv_vault_header_t old; fv_vault_header_store_result_t lr=fv_vault_header_store_load(d,r,h->vault_id,&old); if(lr==FV_VAULT_HEADER_STORE_OK&&(old.sequence==UINT64_MAX||h->sequence!=old.sequence+1u)){clear(&old,sizeof(old));return FV_VAULT_HEADER_STORE_INVALID;} if(lr==FV_VAULT_HEADER_STORE_NOT_FOUND&&h->sequence!=1u)return FV_VAULT_HEADER_STORE_INVALID; if(lr!=FV_VAULT_HEADER_STORE_OK&&lr!=FV_VAULT_HEADER_STORE_NOT_FOUND)return lr;
    uint64_t slot=lr==FV_VAULT_HEADER_STORE_OK?(old.sequence&1u):0u; clear(&old,sizeof(old)); uint8_t block[512]={0}; fv_vault_header_store_result_t sr=fv_vault_header_serialize(h,r,block); if(sr!=FV_VAULT_HEADER_STORE_OK)return sr; if(d->ops->write(d,slot,1u,block)!=FV_BLOCK_OK||d->ops->sync(d)!=FV_BLOCK_OK){clear(block,sizeof(block));return FV_VAULT_HEADER_STORE_IO_ERROR;} uint8_t check[512]; if(d->ops->read(d,slot,1u,check)!=FV_BLOCK_OK||memcmp(block,check,sizeof(block))!=0){clear(block,sizeof(block));clear(check,sizeof(check));return FV_VAULT_HEADER_STORE_IO_ERROR;} fv_vault_header_t parsed;sr=fv_vault_header_parse(check,r,h->vault_id,&parsed);clear(block,sizeof(block));clear(check,sizeof(check));clear(&parsed,sizeof(parsed));return sr;
}

bool fv_vault_header_anchor(const fv_vault_header_t *header,
                            const fv_device_secret_t *roots,
                            fv_security_state_t *state) {
    uint8_t record[FV_VAULT_HEADER_RECORD_SIZE];
    if (state == NULL || fv_vault_header_serialize(header, roots, record) !=
            FV_VAULT_HEADER_STORE_OK) return false;
    state->header_sequence = header->sequence;
    memcpy(state->header_tag, record + TAG_OFFSET, sizeof(state->header_tag));
    clear(record, sizeof(record));
    return true;
}

/* Read only the journal-committed copy. A newer staged copy is not authoritative.
 * The tag binds the whole canonical header, including retries at the same sequence. */
static fv_vault_header_store_result_t find_committed(
    fv_block_device_t *device, const fv_device_secret_t *roots,
    const uint8_t vault_id[FV_VAULT_ID_SIZE], const fv_security_state_t *state,
    fv_vault_header_t *header, uint64_t *slot) {
    if (header) clear(header, sizeof(*header));
    if (!usable(device) || !roots || !state || !header || !state->header_sequence)
        return FV_VAULT_HEADER_STORE_INVALID;
    uint8_t block[FV_BLOCK_SIZE];
    fv_vault_header_store_result_t result = FV_VAULT_HEADER_STORE_INVALID;
    for (uint64_t i = 0; i < FV_VAULT_HEADER_SLOT_COUNT; ++i) {
        if (device->ops->read(device, i, 1u, block) != FV_BLOCK_OK) {
            result = FV_VAULT_HEADER_STORE_IO_ERROR;
            break;
        }
        if (get64(block + 16u) == state->header_sequence &&
            equal(block + TAG_OFFSET, state->header_tag, 32u) &&
            fv_vault_header_parse(block, roots, vault_id, header) == FV_VAULT_HEADER_STORE_OK) {
            if (slot) *slot = i;
            result = FV_VAULT_HEADER_STORE_OK;
            break;
        }
    }
    clear(block, sizeof(block));
    if (result != FV_VAULT_HEADER_STORE_OK) clear(header, sizeof(*header));
    return result;
}

fv_vault_header_store_result_t fv_vault_header_store_load_committed(
    fv_block_device_t *device, const fv_device_secret_t *roots,
    const uint8_t vault_id[FV_VAULT_ID_SIZE], const fv_security_state_t *state,
    fv_vault_header_t *header) {
    if (state == NULL) return FV_VAULT_HEADER_STORE_INVALID;
    if (!state->header_sequence)
        return fv_vault_header_store_load(device, roots, vault_id, header);
    return find_committed(device, roots, vault_id, state, header, NULL);
}

fv_vault_header_store_result_t fv_vault_header_store_stage(
    fv_block_device_t *device, const fv_device_secret_t *roots,
    const fv_security_state_t *state, const fv_vault_header_t *header) {
    if (!state || !header) return FV_VAULT_HEADER_STORE_INVALID;
    if (!state->header_sequence)
        return fv_vault_header_store_update(device, roots, header);
    if (state->header_sequence == UINT64_MAX ||
        header->sequence != state->header_sequence + 1u)
        return FV_VAULT_HEADER_STORE_INVALID;
    fv_vault_header_t committed;
    uint64_t slot = 0u;
    fv_vault_header_store_result_t result = find_committed(
        device, roots, header->vault_id, state, &committed, &slot);
    clear(&committed, sizeof(committed));
    if (result != FV_VAULT_HEADER_STORE_OK) return result;
    uint8_t block[FV_BLOCK_SIZE] = {0}, verified[FV_BLOCK_SIZE] = {0};
    result = fv_vault_header_serialize(header, roots, block);
    if (result == FV_VAULT_HEADER_STORE_OK &&
        (device->ops->write(device, slot ^ 1u, 1u, block) != FV_BLOCK_OK ||
         device->ops->sync(device) != FV_BLOCK_OK ||
         device->ops->read(device, slot ^ 1u, 1u, verified) != FV_BLOCK_OK ||
         !equal(block, verified, sizeof(block))))
        result = FV_VAULT_HEADER_STORE_IO_ERROR;
    clear(block, sizeof(block));
    clear(verified, sizeof(verified));
    return result;
}
