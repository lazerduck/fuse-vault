#include "fuse_vault/fido_store.h"
#include <mbedtls/platform_util.h>
#include <string.h>
#include "image_upgrade.h"

#define BATCH 8u
#define META_SECTORS ((FV_FIDO_IMAGE_SECTORS+14u)/15u)
_Static_assert(FV_FIDO_STORE_BYTES%512u==0, "whole sectors required");
_Static_assert(1u+META_SECTORS+FV_FIDO_IMAGE_SECTORS<=FV_FIDO_BANK_SECTORS, "bank capacity");
_Static_assert(FV_FIDO_REGION_BASE+2u*FV_FIDO_BANK_SECTORS==FV_VOLUME_METADATA_BASE, "FIDO region boundary");
typedef struct {
    fv_pipeline pipeline;
    fv_auth_store sectors;
    uint8_t manifest_key[32];
    unsigned image_sectors;
    alignas(4) uint8_t scratch[BATCH*512];
} bank_context;
typedef struct { uint64_t generation; unsigned bank, image_bytes; uint8_t digest[32]; } snapshot;
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
static int bank_open(bank_context *c,const fv_vault *v,unsigned bank,unsigned image_bytes){
    uint8_t keys[FV_MAX_LAYERS][64]={0},integrity[32]={0};
    fv_algorithm algorithms[FV_MAX_LAYERS]={0};int r=-1;
    memset(c,0,sizeof(*c));
    if(bank>1 || (image_bytes!=65536u && image_bytes!=FV_FIDO_STORE_BYTES))goto done;
    c->image_sectors=image_bytes/512u;
    for(unsigned i=0;i<v->config.volume.layer_count;i++){
        if(i>=FV_MAX_LAYERS || derive(v,"FV2/fido-xts/v1",bank,i,keys[i],fv_cipher_key_bytes((fv_algorithm)v->config.volume.cipher_ids[i])))goto done;
        algorithms[i]=(fv_algorithm)v->config.volume.cipher_ids[i];
    }
    if(derive(v,"FV2/fido-sector-hmac/v1",bank,0,integrity,32) ||
       derive(v,"FV2/fido-manifest/v1",bank,0,c->manifest_key,32) ||
       fv_pipeline_init(&c->pipeline,algorithms,(const uint8_t (*)[64])keys,v->config.volume.layer_count)!=FV_OK ||
       fv_auth_open(&c->sectors,v->platform->sd,base(bank)+1,c->image_sectors,
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
    memset(m,0,512);memcpy(m,"FV2FIDO",8);put(m+8,snap->image_bytes==65536u?1u:2u,4);put(m+12,snap->bank,4);
    put(m+16,snap->generation,8);put(m+24,snap->image_bytes,4);
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
        if(get(m+24,4)!=65536u && get(m+24,4)!=FV_FIDO_STORE_BYTES)continue;
        snapshot candidate={.generation=get(m+16,8),.bank=bank,.image_bytes=(unsigned)get(m+24,4)};memcpy(candidate.digest,m+48,32);
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
    for(unsigned lba=0;lba<c->image_sectors;lba+=BATCH){
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
    if(bank_open(&c,v,snap.bank,snap.image_bytes)){r=FV_FIDO_STORE_INVALID;goto done;}
    r=read_image(&c,image,digest);
    if(!r && !fv_tag_equal(digest,snap.digest))r=FV_FIDO_STORE_CORRUPT;
    if(!r && snap.image_bytes==65536u && !fv_fido_image_upgrade(image))r=FV_FIDO_STORE_CORRUPT;
    if(!r)bind(s,v,&snap);
done:
    bank_close(&c);if(r)mbedtls_platform_zeroize(image,FV_FIDO_STORE_BYTES);return r;
}
static fv_fido_store_result write_snapshot(const fv_vault *v,snapshot *snap,const uint8_t *image){
    bank_context c={0};alignas(4) uint8_t manifest[512]={0},verify[512];uint8_t read_digest[32];
    fv_block_device_t *sd=v->platform->sd;fv_fido_store_result r=FV_FIDO_STORE_IO;
    mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
    if(bank_open(&c,v,snap->bank,snap->image_bytes)){r=FV_FIDO_STORE_INVALID;goto done;}
    /* Invalidate the inactive bank before changing its data or shared tag sectors. */
    if(sd->ops->write(sd,base(snap->bank),1,manifest)!=FV_BLOCK_OK || sd->ops->sync(sd)!=FV_BLOCK_OK ||
       fv_auth_format(&c.sectors)!=FV_BLOCK_OK || mbedtls_sha256_starts(&sha,0))goto done;
    for(unsigned lba=0;lba<c.image_sectors;lba+=BATCH){
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
static fv_fido_store_result initialize_image(fv_fido_store *s,const fv_vault *v,uint8_t image[FV_FIDO_STORE_BYTES]){
    alignas(4) uint8_t empty[512]={0};snapshot snap={.generation=1,.bank=0,.image_bytes=FV_FIDO_STORE_BYTES};
    fv_block_device_t *sd=v->platform->sd;fv_fido_store_result r=FV_FIDO_STORE_IO;
    for(unsigned bank=0;bank<2;bank++)
        if(sd->ops->write(sd,base(bank),1,empty)!=FV_BLOCK_OK)goto done;
    if(sd->ops->sync(sd)!=FV_BLOCK_OK)goto done;
    memset(image,0xff,FV_FIDO_STORE_BYTES);
    r=write_snapshot(v,&snap,image);
    if(!r && s)bind(s,v,&snap);
done:
    if(r)mbedtls_platform_zeroize(image,FV_FIDO_STORE_BYTES);
    return r;
}
fv_fido_store_result fv_fido_store_initialize(fv_fido_store *s,const fv_vault *v,bool confirmed,uint8_t image[FV_FIDO_STORE_BYTES]){
    if(!s || !image)return FV_FIDO_STORE_INVALID;
    fv_fido_store_close(s);memset(image,0,FV_FIDO_STORE_BYTES);
    if(!confirmed)return FV_FIDO_STORE_INVALID;
    if(!vault_valid(v))return FV_FIDO_STORE_LOCKED;
    return initialize_image(s,v,image);
}
fv_fido_store_result fv_fido_store_prepare_new(const fv_vault *v,uint8_t image[FV_FIDO_STORE_BYTES]){
    if(!image)return FV_FIDO_STORE_INVALID;
    memset(image,0,FV_FIDO_STORE_BYTES);
    fv_device_state state;
    /* Only the private creation session, before the ACTIVE commit. In
     * particular, a missing/corrupt store on an existing vault is not blank. */
    if(!v || !v->unlocked || !v->platform || !v->platform->authority.load ||
       !v->platform->sd || !v->platform->sd->ops ||
       !v->platform->sd->ops->read || !v->platform->sd->ops->write ||
       !v->platform->sd->ops->sync || !v->platform->sd->ops->block_count ||
       !v->platform->sd->ops->is_present ||
       !v->platform->sd->ops->is_present(v->platform->sd) ||
       v->platform->sd->ops->block_count(v->platform->sd)<FV_VOLUME_METADATA_BASE ||
       v->platform->authority.load(v->platform->authority.context,&state) ||
       state.status!=FV_ENROLLMENT_EMPTY || state.attempt_pending || state.attempts ||
       state.credential_generation || v->config.credential_generation!=1 ||
       state.token_slot!=v->config.token_slot || memcmp(state.device_id,v->config.device_id,16))
        return FV_FIDO_STORE_LOCKED;
    fv_fido_store_result result=initialize_image(NULL,v,image);
    mbedtls_platform_zeroize(image,FV_FIDO_STORE_BYTES);
    return result;
}
fv_fido_store_result fv_fido_store_commit(fv_fido_store *s,const uint8_t image[FV_FIDO_STORE_BYTES]){
    if(!s || !image){fv_fido_store_close(s);return FV_FIDO_STORE_INVALID;}
    fv_fido_store_result r=FV_FIDO_STORE_LOCKED;
    if(!authorized(s))goto fail;
    snapshot current;r=scan(s->vault,&current);if(r)goto fail;
    if(current.generation!=s->generation || current.bank!=s->bank || !fv_tag_equal(current.digest,s->digest) ||
       current.generation==UINT64_MAX){r=FV_FIDO_STORE_STALE;goto fail;}
    snapshot next={.generation=current.generation+1,.bank=current.bank^1u,.image_bytes=FV_FIDO_STORE_BYTES};
    r=write_snapshot(s->vault,&next,image);if(r)goto fail;
    bind(s,s->vault,&next);return FV_FIDO_STORE_OK;
fail:
    fv_fido_store_close(s);return r;
}

#include "fuse_vault/fido_journal.h"
#define J_DATA (FV_FIDO_DISK_BYTES/512u)
#define J_META ((J_DATA+14u)/15u)
#define J_HOME (J_DATA+J_META)
#define J_START J_HOME
#define J_CONTROL 2046u
#define J_CLEAN 0u
#define J_READY 1u
#define J_RESET 2u
#define J_MIGRATE 3u
_Static_assert(J_DATA%BATCH==0,"whole initialization batches");
_Static_assert(J_HOME==1758u && J_START+256u+19u<J_CONTROL,"journal/migration geometry");
typedef struct {uint64_t seq;unsigned state,count,old_bank,old_bytes;int policy;uint8_t digest[32];} j_control;
static fv_block_device_t *j_sd(fv_fido_journal *s){return s->vault->platform->sd;}
static int j_read(fv_fido_journal *s,unsigned lba,uint8_t *p){return j_sd(s)->ops->read(j_sd(s),FV_FIDO_REGION_BASE+lba,1,p)==FV_BLOCK_OK?0:-1;}
static int j_write(fv_fido_journal *s,unsigned lba,const uint8_t *p){return j_sd(s)->ops->write(j_sd(s),FV_FIDO_REGION_BASE+lba,1,p)==FV_BLOCK_OK?0:-1;}
static int j_sync(fv_fido_journal *s){return j_sd(s)->ops->sync(j_sd(s))==FV_BLOCK_OK?0:-1;}
static bool j_authorized(fv_fido_journal *s){
    uint8_t descriptor[128];
    return s && s->ready && (s->creating || vault_valid(s->vault)) &&
        s->credential_generation==s->vault->config.credential_generation &&
        fv_volume_descriptor_encode(&s->vault->config.volume,UINT64_MAX,descriptor) &&
        !memcmp(descriptor,s->descriptor,128);
}
void fv_fido_journal_close(fv_fido_journal *s){
    if(!s)return;
    fv_pipeline_clear(&s->pipeline);fv_auth_close(&s->auth);fv_hmac_clear(&s->journal_mac);
    mbedtls_platform_zeroize(s,sizeof(*s));
}
static bool j_fail(fv_fido_journal *s){if(s)s->ready=false;return false;}
static int j_control_scan(fv_fido_journal *s,j_control *out){
    alignas(4) uint8_t p[512],tag[32];bool found=false;
    memset(out,0,sizeof(*out));
    for(unsigned i=0;i<2;i++){
        if(j_read(s,J_CONTROL+i,p))return -1;
        if(memcmp(p,"FV3JCTL",8) || get(p+8,4)!=3)continue;
        if(fv_hmac_compute(&s->journal_mac,p,480,NULL,0,tag))return -1;
        if(!fv_tag_equal(tag,p+480))continue;
        j_control c={.seq=get(p+16,8),.state=(unsigned)get(p+24,4),.count=(unsigned)get(p+28,4),
            .old_bank=(unsigned)get(p+32,4),.old_bytes=(unsigned)get(p+36,4),.policy=(int)p[40]-1};
        memcpy(c.digest,p+48,32);
        if(!c.seq || c.seq%2!=i || c.state>J_MIGRATE || c.count>128 || c.policy>1 ||
           (c.state==J_MIGRATE && (c.old_bank>1 || (c.old_bytes!=65536 && c.old_bytes!=131072))))return -1;
        if(!found || c.seq>out->seq){*out=c;found=true;}
    }
    return found?0:1;
}
static int j_control_write(fv_fido_journal *s,j_control *c){
    alignas(4) uint8_t p[512]={0},check[512];
    if(s->sequence==UINT64_MAX)return -1;
    c->seq=s->sequence+1;
    memcpy(p,"FV3JCTL",8);put(p+8,3,4);put(p+16,c->seq,8);put(p+24,c->state,4);
    put(p+28,c->count,4);put(p+32,c->old_bank,4);put(p+36,c->old_bytes,4);p[40]=(uint8_t)(c->policy+1);
    memcpy(p+48,c->digest,32);
    if(fv_hmac_compute(&s->journal_mac,p,480,NULL,0,p+480) ||
       j_write(s,J_CONTROL+(unsigned)(c->seq%2),p) || j_sync(s) ||
       j_read(s,J_CONTROL+(unsigned)(c->seq%2),check) || memcmp(p,check,512))return -1;
    s->sequence=c->seq;return 0;
}
static int j_entry_read(fv_fido_journal *s,unsigned slot,uint64_t seq,unsigned *target,uint8_t payload[512],uint8_t header[512]){
    uint8_t tag[32];
    if(slot>=128 || j_read(s,J_START+slot*2,header) || j_read(s,J_START+slot*2+1,payload))return -1;
    *target=(unsigned)get(header+24,4);
    if(memcmp(header,"FV3JREC",8) || get(header+8,8)!=seq || get(header+16,4)!=slot || *target>=J_HOME ||
       fv_hmac_compute(&s->journal_mac,header,480,payload,512,tag) || !fv_tag_equal(tag,header+480))return -1;
    return 0;
}
static fv_block_result_t j_overlay_read(fv_block_device_t *d,uint64_t lba,uint32_t n,uint8_t *p){
    fv_fido_journal *s=d->context;
    if(lba>J_HOME || n>J_HOME-lba)return FV_BLOCK_ERROR_OUT_OF_RANGE;
    if(!s->count)return j_sd(s)->ops->read(j_sd(s),FV_FIDO_REGION_BASE+lba,n,p);
    for(unsigned i=0;i<n;i++){
        unsigned k=0;for(;k<s->count;k++)if(s->targets[k]==lba+i)break;
        if(k<s->count){alignas(4) uint8_t h[512];unsigned target;
            if(j_entry_read(s,k,s->sequence+1,&target,p+i*512,h) || target!=lba+i)return FV_BLOCK_ERROR_IO;
        }else if(j_read(s,(unsigned)lba+i,p+i*512))return FV_BLOCK_ERROR_IO;
    }
    return FV_BLOCK_OK;
}
static fv_block_result_t j_overlay_write(fv_block_device_t *d,uint64_t lba,uint32_t n,const uint8_t *p){
    fv_fido_journal *s=d->context;
    if(lba>J_HOME || n>J_HOME-lba)return FV_BLOCK_ERROR_OUT_OF_RANGE;
    if(s->recovering)return j_sd(s)->ops->write(j_sd(s),FV_FIDO_REGION_BASE+lba,n,p);
    if(!s->count){j_control c;
        if(j_control_scan(s,&c) || c.seq!=s->sequence || c.state!=J_CLEAN)return FV_BLOCK_ERROR_IO;
    }
    for(unsigned i=0;i<n;i++){
        unsigned k=0;for(;k<s->count;k++)if(s->targets[k]==lba+i)break;
        if(k==128 || s->sequence==UINT64_MAX)return FV_BLOCK_ERROR_IO;
        alignas(4) uint8_t h[512]={0};memcpy(h,"FV3JREC",8);put(h+8,s->sequence+1,8);put(h+16,k,4);put(h+24,lba+i,4);
        if(fv_hmac_compute(&s->journal_mac,h,480,p+i*512,512,h+480) ||
           j_write(s,J_START+k*2+1,p+i*512) || j_write(s,J_START+k*2,h))return FV_BLOCK_ERROR_IO;
        s->targets[k]=(uint16_t)(lba+i);if(k==s->count)s->count++;
    }
    return FV_BLOCK_OK;
}
static fv_block_result_t j_overlay_sync(fv_block_device_t *d){return j_sync(d->context)?FV_BLOCK_ERROR_IO:FV_BLOCK_OK;}
static uint64_t j_overlay_count(const fv_block_device_t *d){(void)d;return J_HOME;}
static bool j_overlay_present(const fv_block_device_t *d){fv_fido_journal *s=d->context;return j_sd(s)->ops->is_present(j_sd(s));}
static const fv_block_device_ops_t j_ops={j_overlay_read,j_overlay_write,j_overlay_sync,j_overlay_count,j_overlay_present};
static int j_bind(fv_fido_journal *s,const fv_vault *v){
    uint8_t keys[FV_MAX_LAYERS][64]={0},key[32];fv_algorithm algorithms[FV_MAX_LAYERS]={0};int r=-1;
    memset(s,0,sizeof(*s));s->vault=v;s->credential_generation=v->config.credential_generation;
    s->overlay=(fv_block_device_t){&j_ops,s};
    if(!fv_volume_descriptor_encode(&v->config.volume,UINT64_MAX,s->descriptor))goto done;
    for(unsigned i=0;i<v->config.volume.layer_count;i++){
        algorithms[i]=(fv_algorithm)v->config.volume.cipher_ids[i];
        if(derive(v,"FV3/fido-xts",0,i,keys[i],fv_cipher_key_bytes(algorithms[i])))goto done;
    }
    if(fv_pipeline_init(&s->pipeline,algorithms,(const uint8_t (*)[64])keys,v->config.volume.layer_count)!=FV_OK ||
       derive(v,"FV3/fido-sector",0,0,key,32) ||
       fv_auth_open(&s->auth,&s->overlay,0,J_DATA,v->config.volume.volume_id,key,NULL)!=FV_BLOCK_OK ||
       derive(v,"FV3/fido-journal",0,0,key,32) || fv_hmac_init(&s->journal_mac,key,32))goto done;
    s->ready=true;r=0;
 done:mbedtls_platform_zeroize(keys,sizeof(keys));mbedtls_platform_zeroize(key,sizeof(key));
    if(r)fv_fido_journal_close(s);
    return r;
}
static bool j_sector(fv_fido_journal *s,unsigned lba,uint8_t p[512],bool write){
    uint64_t unset=0;unsigned slot=lba%8;uint8_t bit=(uint8_t)(1u<<slot);
    if(write){s->cache_valid&=(uint8_t)~bit;return fv_pipeline_encrypt(&s->pipeline,lba,p,p)==FV_OK && fv_auth_write(&s->auth,lba,1,p)==FV_BLOCK_OK;}
    if((s->cache_valid&bit) && s->cache_lba[slot]==lba){memcpy(p,s->cache[slot],512);return true;}
    if(fv_auth_read(&s->auth,lba,1,p,&unset)!=FV_BLOCK_OK || unset || fv_pipeline_decrypt(&s->pipeline,lba,p,p)!=FV_OK)return false;
    memcpy(s->cache[slot],p,512);s->cache_lba[slot]=(uint16_t)lba;s->cache_valid|=bit;return true;
}
static bool j_access(fv_fido_journal *s,size_t offset,uint8_t *out,const uint8_t *in,size_t n){
    alignas(4) uint8_t p[512];bool ok=false;
    if(!j_authorized(s) || offset>FV_FIDO_DISK_BYTES || n>FV_FIDO_DISK_BYTES-offset)goto done;
    while(n){unsigned lba=(unsigned)(offset/512),at=(unsigned)(offset%512);size_t take=512-at;if(take>n)take=n;
        if(!j_sector(s,lba,p,false))goto done;
        if(in){memcpy(p+at,in,take);if(!j_sector(s,lba,p,true))goto done;in+=take;}
        else {memcpy(out,p+at,take);out+=take;}
        offset+=take;n-=take;
    }ok=true;
 done:mbedtls_platform_zeroize(p,sizeof(p));if(!ok)j_fail(s);return ok;
}
bool fv_fido_journal_read(void *ctx,size_t offset,uint8_t *p,size_t n){
    if(!p && n)return false;
    bool ok=j_access(ctx,offset,p,NULL,n);if(!ok && p)mbedtls_platform_zeroize(p,n);return ok;
}
bool fv_fido_journal_write(void *ctx,size_t offset,const uint8_t *p,size_t n){return (!n || p) && j_access(ctx,offset,NULL,p,n);}
static int j_digest(fv_fido_journal *s,const j_control *c,uint8_t digest[32]){
    alignas(4) uint8_t h[512],p[512];uint16_t seen[128];int r=-1;mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
    if(mbedtls_sha256_starts(&sha,0))goto done;
    for(unsigned i=0;i<c->count;i++){unsigned target;
        if(j_entry_read(s,i,c->seq,&target,p,h))goto done;
        for(unsigned k=0;k<i;k++)if(seen[k]==target)goto done;
        seen[i]=(uint16_t)target;
        if(mbedtls_sha256_update(&sha,h,512) || mbedtls_sha256_update(&sha,p,512))goto done;
    }
    r=mbedtls_sha256_finish(&sha,digest);
 done:mbedtls_sha256_free(&sha);mbedtls_platform_zeroize(p,sizeof(p));return r;
}
static int j_replay(fv_fido_journal *s,const j_control *c){
    alignas(4) uint8_t digest[32],p[512],h[512],check[512];
    if(j_digest(s,c,digest) || !fv_tag_equal(digest,c->digest))return -1;
    for(unsigned i=0;i<c->count;i++){unsigned target;
        if(j_entry_read(s,i,c->seq,&target,p,h) || j_write(s,target,p))return -1;
    }
    if(j_sync(s))return -1;
    for(unsigned i=0;i<c->count;i++){unsigned target;
        if(j_entry_read(s,i,c->seq,&target,p,h) || j_read(s,target,check) || memcmp(p,check,512))return -1;
    }
    j_control clean={.state=J_CLEAN,.policy=-1};
    if(j_control_write(s,&clean))return -1;
    s->count=0;s->auth.cache_count=0;return 0;
}
bool fv_fido_journal_commit(void *ctx){
    fv_fido_journal *s=ctx;j_control current;
    if(!j_authorized(s) || j_control_scan(s,&current) || current.seq!=s->sequence || current.state!=J_CLEAN)return j_fail(s);
    if(!s->count)return true;
    j_control c={.seq=s->sequence+1,.state=J_READY,.count=s->count,.policy=-1};
    if(j_sync(s) || j_digest(s,&c,c.digest) || j_control_write(s,&c) || j_replay(s,&c))return j_fail(s);
    return true;
}
static int j_verify_home(fv_fido_journal *s){
    alignas(4) uint8_t batch[BATCH*512];int r=-1;
    s->auth.cache_count=0;s->cache_valid=0;
    for(unsigned i=0;i<J_DATA;i+=BATCH){uint64_t unset=0;
        if(fv_auth_read(&s->auth,i,BATCH,batch,&unset)!=FV_BLOCK_OK || unset)goto done;
    }
    r=0;
 done:mbedtls_platform_zeroize(batch,sizeof(batch));return r;
}
static int j_blank(fv_fido_journal *s,int policy){
    alignas(4) uint8_t p[BATCH*512];s->recovering=true;s->count=0;s->auth.cache_count=0;s->cache_valid=0;
    if(fv_auth_format(&s->auth)!=FV_BLOCK_OK)return -1;
    for(unsigned i=0;i<J_DATA;i+=BATCH){
        memset(p,255,sizeof(p));
        for(unsigned k=0;k<BATCH;k++)if(fv_pipeline_encrypt(&s->pipeline,i+k,p+k*512,p+k*512)!=FV_OK)return -1;
        if(fv_auth_write(&s->auth,i,BATCH,p)!=FV_BLOCK_OK)return -1;
    }
    if(policy>=0){
        const size_t rom=FV_FIDO_DISK_BYTES-12,data=rom-16384-8,record=data-14;
        uint8_t zero[12]={0},r[14]={0};put(r+4,4096+data,4);put(r+8,0x1123,2);put(r+10,2,2);r[12]=1;r[13]=(uint8_t)policy;
        if(!j_access(s,rom,NULL,zero,12) || !j_access(s,record,NULL,r,14))return -1;
        put(zero,4096+record,4);if(!j_access(s,data,NULL,zero,8))return -1;
    }
    if(j_sync(s))return -1;
    if(j_verify_home(s)){mbedtls_platform_zeroize(p,sizeof(p));return -1;}
    mbedtls_platform_zeroize(p,sizeof(p));return 0;
}
static int j_finish_reset(fv_fido_journal *s,const j_control *c){
    if(j_blank(s,c->policy))return -1;
    j_control clean={.state=J_CLEAN,.policy=-1};
    if(j_control_write(s,&clean))return -1;
    s->recovering=false;return 0;
}
bool fv_fido_journal_reset(void *ctx,int policy){
    fv_fido_journal *s=ctx;j_control current;
    if(policy < -1 || policy>1 || !j_authorized(s) || j_control_scan(s,&current) || current.seq!=s->sequence || current.state!=J_CLEAN)return j_fail(s);
    s->count=0;s->auth.cache_count=0;
    j_control reset={.state=J_RESET,.policy=policy};
    if(j_control_write(s,&reset) || j_finish_reset(s,&reset))return j_fail(s);
    return true;
}
/* Read the staged legacy snapshot using its original bank's keys and addresses. */
static int j_old_open(fv_fido_journal *s,const j_control *c,bank_context *old){
    if(bank_open(old,s->vault,c->old_bank,c->old_bytes))return -1;
    old->sectors.base=FV_FIDO_REGION_BASE+J_START+1;
    old->sectors.data_base=old->sectors.base+old->sectors.metadata_blocks;
    return 0;
}
static int j_old_read(bank_context *old,size_t offset,uint8_t *out,size_t n){
    while(n){unsigned lba=(unsigned)(offset/512),at=(unsigned)(offset%512);size_t take=512-at;uint64_t unset=0;if(take>n)take=n;
        if(fv_auth_read(&old->sectors,lba,1,old->scratch,&unset)!=FV_BLOCK_OK || unset ||
           fv_pipeline_decrypt(&old->pipeline,lba,old->scratch,old->scratch)!=FV_OK)return -1;
        memcpy(out,old->scratch+at,take);offset+=take;out+=take;n-=take;
    }return 0;
}
static int j_migrate_finish(fv_fido_journal *s,const j_control *c){
    bank_context old={0};alignas(4) uint8_t digest[32],p[512];int r=-1;
    if(j_old_open(s,c,&old) || read_image(&old,NULL,digest) || !fv_tag_equal(digest,c->digest))goto done;
    /* The source is outside the home region and survives every destination write. */
    if(j_blank(s,-1))goto done;
    size_t delta=FV_FIDO_DISK_BYTES-c->old_bytes;
    for(size_t at=0;at<c->old_bytes;at+=512){
        if(j_old_read(&old,at,p,512) || !j_access(s,delta+at,NULL,p,512))goto done;
    }
    size_t rom=c->old_bytes-12,data=rom-16384-8;
    bool blank=true;
    for(size_t at=0;at<c->old_bytes && blank;at+=512){if(j_old_read(&old,at,p,512))goto done;for(unsigned i=0;i<512;i++)if(p[i]!=255){blank=false;break;}}
    if(!blank)for(unsigned region=0;region<2;region++){
        size_t end=region?rom:data,start=region?rom-16384:0,previous=end;
        if(j_old_read(&old,end,p,4))goto done;
        uint32_t next=(uint32_t)get(p,4);put(p,next?next+delta:0,4);
        if(!j_access(s,delta+end,NULL,p,4))goto done;
        while(next){
            if(next<4096+start || next>=4096+previous)goto done;
            size_t at=next-4096;if(previous-at<12 || j_old_read(&old,at,p,12) || get(p+4,4)!=4096+previous)goto done;
            size_t len=(size_t)get(p+10,2),header=12;
            if(len==65535){if(previous-at<16 || j_old_read(&old,at+12,p+12,4))goto done;len=(size_t)get(p+12,4);header=16;}
            if(len>previous-at-header)goto done;
            next=(uint32_t)get(p,4);put(p,next?next+delta:0,4);put(p+4,4096+previous+delta,4);
            if(!j_access(s,delta+at,NULL,p,8))goto done;
            previous=at;
        }
    }
    if(j_sync(s))goto done;
    /* Authenticate every destination sector before publishing the new format. */
    if(j_verify_home(s))goto done;
    j_control clean={.state=J_CLEAN,.policy=-1};if(j_control_write(s,&clean))goto done;
    /* Supersede the migration marker before its source becomes reusable journal. */
    if(j_control_write(s,&clean))goto done;
    s->recovering=false;r=0;
 done:bank_close(&old);mbedtls_platform_zeroize(p,sizeof(p));return r;
}
static int j_migrate_start(fv_fido_journal *s){
    snapshot snap;bank_context old={0};alignas(4) uint8_t digest[32],p[512];int r=-1;
    if(scan(s->vault,&snap) || bank_open(&old,s->vault,snap.bank,snap.image_bytes) ||
       read_image(&old,NULL,digest) || !fv_tag_equal(digest,snap.digest))goto done;
    unsigned blocks=1+(snap.image_bytes/512+14)/15+snap.image_bytes/512;
    for(unsigned i=0;i<blocks;i++){
        if(j_sd(s)->ops->read(j_sd(s),base(snap.bank)+i,1,p)!=FV_BLOCK_OK || j_write(s,J_START+i,p))goto done;
    }
    if(j_sync(s))goto done;
    j_control c={.state=J_MIGRATE,.old_bank=snap.bank,.old_bytes=snap.image_bytes,.policy=-1};memcpy(c.digest,snap.digest,32);
    bank_close(&old);
    if(j_old_open(s,&c,&old) || read_image(&old,NULL,digest) || !fv_tag_equal(digest,c.digest) || j_control_write(s,&c))goto done;
    r=j_migrate_finish(s,&c);
 done:bank_close(&old);mbedtls_platform_zeroize(p,sizeof(p));return r;
}
int fv_fido_journal_open(fv_fido_journal *s,const fv_vault *v){
    if(!s)return -1;
    fv_fido_journal_close(s);if(!vault_valid(v) || j_bind(s,v))return -1;
    j_control c;int r=j_control_scan(s,&c);
    if(r<0)goto fail;
    if(r==1){if(j_migrate_start(s))goto fail;return 0;}
    s->sequence=c.seq;
    if((c.state==J_READY && j_replay(s,&c)) || (c.state==J_RESET && j_finish_reset(s,&c)) ||
       (c.state==J_MIGRATE && j_migrate_finish(s,&c)))goto fail;
    return 0;
 fail:fv_fido_journal_close(s);return -1;
}
static int j_initialize(fv_fido_journal *s,const fv_vault *v,bool creating){
    if(j_bind(s,v))return -1;
    s->creating=creating;j_control c;int r=j_control_scan(s,&c);if(r<0)goto fail;
    if(!r)s->sequence=c.seq;
    j_control reset={.state=J_RESET,.policy=-1};
    if(j_control_write(s,&reset) || j_finish_reset(s,&reset))goto fail;
    return 0;
 fail:fv_fido_journal_close(s);return -1;
}
int fv_fido_journal_initialize(fv_fido_journal *s,const fv_vault *v,bool confirmed){
    if(!s)return -1;
    fv_fido_journal_close(s);return confirmed && vault_valid(v)?j_initialize(s,v,false):-1;
}
int fv_fido_journal_prepare_new(fv_fido_journal *s,const fv_vault *v){
    if(!s)return -1;
    fv_device_state state;fv_fido_journal_close(s);
    if(!v || !v->unlocked || !v->platform || !v->platform->sd || !v->platform->sd->ops ||
       !v->platform->sd->ops->read || !v->platform->sd->ops->write || !v->platform->sd->ops->sync ||
       !v->platform->sd->ops->block_count || !v->platform->sd->ops->is_present ||
       !v->platform->sd->ops->is_present(v->platform->sd) || v->platform->sd->ops->block_count(v->platform->sd)<2064 ||
       !v->platform->authority.load ||
       v->platform->authority.load(v->platform->authority.context,&state) || state.status!=FV_ENROLLMENT_EMPTY ||
       state.attempt_pending || state.attempts || state.credential_generation || v->config.credential_generation!=1 ||
       state.token_slot!=v->config.token_slot || memcmp(state.device_id,v->config.device_id,16))return -1;
    int r=j_initialize(s,v,true);fv_fido_journal_close(s);return r;
}
int fv_fido_journal_key(fv_fido_journal *s,uint8_t key[32]){
    if(!key)return -1;
    memset(key,0,32);return j_authorized(s)?derive(s->vault,"FV2/fido-engine/v1",0,0,key,32):-1;
}
