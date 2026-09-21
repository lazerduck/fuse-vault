#include "fuse_vault/fido_store.h"
#include <mbedtls/platform_util.h>
#include <string.h>

#define BATCH 8u
#define META_SECTORS ((FV_FIDO_IMAGE_SECTORS+14u)/15u)
_Static_assert(FV_FIDO_STORE_BYTES%512u==0, "whole sectors required");
_Static_assert(1u+META_SECTORS+FV_FIDO_IMAGE_SECTORS<=FV_FIDO_BANK_SECTORS, "bank capacity");
_Static_assert(FV_FIDO_REGION_BASE+2u*FV_FIDO_BANK_SECTORS==FV_VOLUME_METADATA_BASE, "FIDO region boundary");
typedef struct {
    fv_pipeline pipeline;
    fv_auth_store sectors;
    uint8_t manifest_key[32];
    alignas(4) uint8_t scratch[BATCH*512];
} bank_context;
typedef struct { uint64_t generation; unsigned bank; uint8_t digest[32]; } snapshot;
static uint64_t base(unsigned bank){return FV_FIDO_REGION_BASE+(uint64_t)bank*FV_FIDO_BANK_SECTORS;}
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
void fv_fido_store_close(fv_fido_store *s){if(s)mbedtls_platform_zeroize(s,sizeof(*s));}
static bool vault_valid(const fv_vault *v){
    if(!(v && v->unlocked && v->platform && v->platform->sd &&
        v->platform->sd->ops && v->platform->sd->ops->read && v->platform->sd->ops->write &&
        v->platform->sd->ops->sync && v->platform->sd->ops->block_count &&
        v->platform->sd->ops->is_present && v->platform->sd->ops->is_present(v->platform->sd) &&
        v->platform->sd->ops->block_count(v->platform->sd)>=FV_VOLUME_METADATA_BASE &&
        v->platform->authority.load))return false;
    fv_device_state state;
    return !v->platform->authority.load(v->platform->authority.context,&state) &&
        state.status==FV_ENROLLMENT_ACTIVE && !state.attempt_pending &&
        state.credential_generation==v->config.credential_generation &&
        state.token_slot==v->config.token_slot &&
        !memcmp(state.volume_id,v->config.volume.volume_id,16) &&
        !memcmp(state.device_id,v->config.device_id,16);
}
static bool authorized(const fv_fido_store *s){
    uint8_t descriptor[FV_VOLUME_DESCRIPTOR_BYTES];
    return s && s->ready && vault_valid(s->vault) &&
        s->credential_generation==s->vault->config.credential_generation &&
        fv_volume_descriptor_encode(&s->vault->config.volume,UINT64_MAX,descriptor) &&
        !memcmp(s->descriptor,descriptor,sizeof(descriptor));
}
/* HKDF context is fixed-width: versioned purpose including NUL, descriptor hash,
 * uint32 bank, uint32 layer. All FIDO and USB purposes are disjoint. */
static int derive(const fv_vault *v,const char *purpose,unsigned bank,unsigned layer,uint8_t *out,size_t n){
    uint8_t descriptor[128],hash[32],prk[32],info[128];int r=-1;
    size_t label=strlen(purpose)+1;
    if(label+40>sizeof(info) || !fv_volume_descriptor_encode(&v->config.volume,UINT64_MAX,descriptor) ||
       mbedtls_sha256(descriptor,sizeof(descriptor),hash,0) ||
       fv_hkdf_extract(v->config.volume.volume_id,16,v->vmk,32,prk))goto done;
    memcpy(info,purpose,label);memcpy(info+label,hash,32);
    put(info+label+32,bank,4);put(info+label+36,layer,4);
    r=fv_hkdf_expand(prk,info,label+40,out,n);
done:
    mbedtls_platform_zeroize(prk,sizeof(prk));mbedtls_platform_zeroize(info,sizeof(info));
    if(r)mbedtls_platform_zeroize(out,n);
    return r;
}
fv_fido_store_result fv_fido_store_engine_key(const fv_fido_store *s,uint8_t key[32]){
    if(!key)return FV_FIDO_STORE_INVALID;
    memset(key,0,32);
    if(!authorized(s))return FV_FIDO_STORE_LOCKED;
    return derive(s->vault,"FV2/fido-engine/v1",0,0,key,32)?FV_FIDO_STORE_INVALID:FV_FIDO_STORE_OK;
}
static void bank_close(bank_context *c){
    fv_pipeline_clear(&c->pipeline);fv_auth_close(&c->sectors);
    mbedtls_platform_zeroize(c,sizeof(*c));
}
static int bank_open(bank_context *c,const fv_vault *v,unsigned bank){
    uint8_t keys[FV_MAX_LAYERS][64]={0},integrity[32]={0};
    fv_algorithm algorithms[FV_MAX_LAYERS]={0};int r=-1;
    memset(c,0,sizeof(*c));
    if(bank>1)goto done;
    for(unsigned i=0;i<v->config.volume.layer_count;i++){
        if(i>=FV_MAX_LAYERS || derive(v,"FV2/fido-xts/v1",bank,i,keys[i],64))goto done;
        algorithms[i]=(fv_algorithm)v->config.volume.cipher_ids[i];
    }
    if(derive(v,"FV2/fido-sector-hmac/v1",bank,0,integrity,32) ||
       derive(v,"FV2/fido-manifest/v1",bank,0,c->manifest_key,32) ||
       fv_pipeline_init(&c->pipeline,algorithms,(const uint8_t (*)[64])keys,v->config.volume.layer_count)!=FV_OK ||
       fv_auth_open(&c->sectors,v->platform->sd,base(bank)+1,FV_FIDO_IMAGE_SECTORS,
           v->config.volume.volume_id,integrity,NULL)!=FV_BLOCK_OK)goto done;
    r=0;
done:
    mbedtls_platform_zeroize(keys,sizeof(keys));mbedtls_platform_zeroize(integrity,sizeof(integrity));
    if(r)bank_close(c);
    return r;
}
static int manifest_tag(const uint8_t key[32],const uint8_t m[512],uint8_t tag[32]){
    static const uint8_t domain[]="FV2/fido-commit/v1";
    fv_hmac h={0};int r=fv_hmac_init(&h,key,32);
    if(!r)r=fv_hmac_compute(&h,domain,sizeof(domain),m,480,tag);
    fv_hmac_clear(&h);return r;
}
static int encode_manifest(const fv_vault *v,const snapshot *snap,const uint8_t key[32],uint8_t m[512]){
    uint8_t descriptor[128];
    memset(m,0,512);memcpy(m,"FV2FIDO",8);put(m+8,1,4);put(m+12,snap->bank,4);
    put(m+16,snap->generation,8);put(m+24,FV_FIDO_STORE_BYTES,4);
    memcpy(m+32,v->config.volume.volume_id,16);memcpy(m+48,snap->digest,32);
    if(!fv_volume_descriptor_encode(&v->config.volume,UINT64_MAX,descriptor) ||
       mbedtls_sha256(descriptor,sizeof(descriptor),m+80,0))return -1;
    return manifest_tag(key,m,m+480);
}
/* Invalid/torn manifests are not commits. Physical read errors are never absence.
 * If the newest authentic manifest's payload is corrupt, open fails closed rather
 * than silently falling back. Deliberate replay/removal remains out of scope. */
static fv_fido_store_result scan(const fv_vault *v,snapshot *latest){
    alignas(4) uint8_t m[512],canonical[512];uint8_t key[32];
    bool found=false;fv_fido_store_result r=FV_FIDO_STORE_CORRUPT;
    memset(latest,0,sizeof(*latest));
    for(unsigned bank=0;bank<2;bank++){
        if(v->platform->sd->ops->read(v->platform->sd,base(bank),1,m)!=FV_BLOCK_OK){r=FV_FIDO_STORE_IO;goto done;}
        snapshot candidate={.generation=get(m+16,8),.bank=bank};memcpy(candidate.digest,m+48,32);
        if(derive(v,"FV2/fido-manifest/v1",bank,0,key,32) ||
           encode_manifest(v,&candidate,key,canonical)){r=FV_FIDO_STORE_INVALID;goto done;}
        if(!candidate.generation || memcmp(m,canonical,480) || !fv_tag_equal(m+480,canonical+480))continue;
        if(found && candidate.generation==latest->generation){r=FV_FIDO_STORE_CORRUPT;goto done;}
        if(!found || candidate.generation>latest->generation){*latest=candidate;found=true;}
    }
    r=found?FV_FIDO_STORE_OK:FV_FIDO_STORE_CORRUPT;
done:
    mbedtls_platform_zeroize(key,sizeof(key));return r;
}
/* Authenticate each ciphertext batch and hash the whole ciphertext snapshot.
 * Output is unusable until open succeeds after the final manifest digest check;
 * open wipes the entire output on any failure. */
static fv_fido_store_result read_image(bank_context *c,uint8_t *output,uint8_t digest[32]){
    mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
    fv_fido_store_result r=FV_FIDO_STORE_IO;
    if(mbedtls_sha256_starts(&sha,0))goto done;
    for(unsigned lba=0;lba<FV_FIDO_IMAGE_SECTORS;lba+=BATCH){
        uint64_t unset=0;
        fv_block_result_t br=fv_auth_read(&c->sectors,lba,BATCH,c->scratch,&unset);
        if(br!=FV_BLOCK_OK || unset){
            r=(br==FV_BLOCK_ERROR_INTEGRITY || (br==FV_BLOCK_OK && unset))?
                FV_FIDO_STORE_CORRUPT:FV_FIDO_STORE_IO;
            goto done;
        }
        if(mbedtls_sha256_update(&sha,c->scratch,sizeof(c->scratch)))goto done;
        if(output){
            for(unsigned i=0;i<BATCH;i++)
                if(fv_pipeline_decrypt(&c->pipeline,lba+i,c->scratch+i*512,c->scratch+i*512)!=FV_OK)goto done;
            memcpy(output+lba*512,c->scratch,sizeof(c->scratch));
        }
    }
    if(!mbedtls_sha256_finish(&sha,digest))r=FV_FIDO_STORE_OK;
done:
    mbedtls_sha256_free(&sha);mbedtls_platform_zeroize(c->scratch,sizeof(c->scratch));return r;
}
static void bind(fv_fido_store *s,const fv_vault *v,const snapshot *snap){
    memset(s,0,sizeof(*s));s->vault=v;s->credential_generation=v->config.credential_generation;
    s->generation=snap->generation;s->bank=snap->bank;memcpy(s->digest,snap->digest,32);
    (void)fv_volume_descriptor_encode(&v->config.volume,UINT64_MAX,s->descriptor);s->ready=true;
}
fv_fido_store_result fv_fido_store_open(fv_fido_store *s,const fv_vault *v,uint8_t image[FV_FIDO_STORE_BYTES]){
    if(!s || !image)return FV_FIDO_STORE_INVALID;
    fv_fido_store_close(s);memset(image,0,FV_FIDO_STORE_BYTES);
    if(!vault_valid(v))return FV_FIDO_STORE_LOCKED;
    snapshot snap;bank_context c={0};uint8_t digest[32];
    fv_fido_store_result r=scan(v,&snap);if(r)goto done;
    if(bank_open(&c,v,snap.bank)){r=FV_FIDO_STORE_INVALID;goto done;}
    r=read_image(&c,image,digest);
    if(!r && !fv_tag_equal(digest,snap.digest))r=FV_FIDO_STORE_CORRUPT;
    if(!r)bind(s,v,&snap);
done:
    bank_close(&c);if(r)mbedtls_platform_zeroize(image,FV_FIDO_STORE_BYTES);return r;
}
static fv_fido_store_result write_snapshot(const fv_vault *v,snapshot *snap,const uint8_t *image){
    bank_context c={0};alignas(4) uint8_t manifest[512]={0},verify[512];uint8_t read_digest[32];
    fv_block_device_t *sd=v->platform->sd;fv_fido_store_result r=FV_FIDO_STORE_IO;
    mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
    if(bank_open(&c,v,snap->bank)){r=FV_FIDO_STORE_INVALID;goto done;}
    /* Invalidate the inactive bank before changing its data or shared tag sectors. */
    if(sd->ops->write(sd,base(snap->bank),1,manifest)!=FV_BLOCK_OK || sd->ops->sync(sd)!=FV_BLOCK_OK ||
       fv_auth_format(&c.sectors)!=FV_BLOCK_OK || mbedtls_sha256_starts(&sha,0))goto done;
    for(unsigned lba=0;lba<FV_FIDO_IMAGE_SECTORS;lba+=BATCH){
        memcpy(c.scratch,image+lba*512,sizeof(c.scratch));
        for(unsigned i=0;i<BATCH;i++)
            if(fv_pipeline_encrypt(&c.pipeline,lba+i,c.scratch+i*512,c.scratch+i*512)!=FV_OK)goto done;
        if(mbedtls_sha256_update(&sha,c.scratch,sizeof(c.scratch)) ||
           fv_auth_write(&c.sectors,lba,BATCH,c.scratch)!=FV_BLOCK_OK)goto done;
    }
    if(mbedtls_sha256_finish(&sha,snap->digest) || sd->ops->sync(sd)!=FV_BLOCK_OK)goto done;
    /* Drop tag cache before readback: verify media, not our own cached tags. */
    c.sectors.cache_count=0;
    r=read_image(&c,NULL,read_digest);if(r)goto done;
    r=FV_FIDO_STORE_IO;
    if(!fv_tag_equal(read_digest,snap->digest) || encode_manifest(v,snap,c.manifest_key,manifest) ||
       sd->ops->write(sd,base(snap->bank),1,manifest)!=FV_BLOCK_OK || sd->ops->sync(sd)!=FV_BLOCK_OK ||
       sd->ops->read(sd,base(snap->bank),1,verify)!=FV_BLOCK_OK || memcmp(manifest,verify,512))goto done;
    r=FV_FIDO_STORE_OK;
done:
    mbedtls_sha256_free(&sha);bank_close(&c);return r;
}
fv_fido_store_result fv_fido_store_initialize(fv_fido_store *s,const fv_vault *v,bool confirmed,uint8_t image[FV_FIDO_STORE_BYTES]){
    if(!s || !image)return FV_FIDO_STORE_INVALID;
    fv_fido_store_close(s);memset(image,0,FV_FIDO_STORE_BYTES);
    if(!confirmed)return FV_FIDO_STORE_INVALID;
    if(!vault_valid(v))return FV_FIDO_STORE_LOCKED;
    alignas(4) uint8_t empty[512]={0};snapshot snap={.generation=1,.bank=0};
    fv_block_device_t *sd=v->platform->sd;fv_fido_store_result r=FV_FIDO_STORE_IO;
    for(unsigned bank=0;bank<2;bank++)
        if(sd->ops->write(sd,base(bank),1,empty)!=FV_BLOCK_OK)goto done;
    if(sd->ops->sync(sd)!=FV_BLOCK_OK)goto done;
    memset(image,0xff,FV_FIDO_STORE_BYTES);
    r=write_snapshot(v,&snap,image);
    if(!r)bind(s,v,&snap);
done:
    if(r)mbedtls_platform_zeroize(image,FV_FIDO_STORE_BYTES);
    return r;
}
fv_fido_store_result fv_fido_store_commit(fv_fido_store *s,const uint8_t image[FV_FIDO_STORE_BYTES]){
    if(!s || !image){fv_fido_store_close(s);return FV_FIDO_STORE_INVALID;}
    fv_fido_store_result r=FV_FIDO_STORE_LOCKED;
    if(!authorized(s))goto fail;
    snapshot current;r=scan(s->vault,&current);if(r)goto fail;
    if(current.generation!=s->generation || current.bank!=s->bank || !fv_tag_equal(current.digest,s->digest) ||
       current.generation==UINT64_MAX){r=FV_FIDO_STORE_STALE;goto fail;}
    snapshot next={.generation=current.generation+1,.bank=current.bank^1u};
    r=write_snapshot(s->vault,&next,image);if(r)goto fail;
    bind(s,s->vault,&next);return FV_FIDO_STORE_OK;
fail:
    fv_fido_store_close(s);return r;
}
