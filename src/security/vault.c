#include "fuse_vault/vault.h"
#include <mbedtls/platform_util.h>
#include <string.h>

static bool platform_valid(const fv_vault_platform *p) {
    return p && p->authority.load && p->authority.commit && p->authority.binding &&
        p->kdf_limits.minimum && p->kdf_limits.maximum>=p->kdf_limits.minimum;
}
static bool media_valid(const fv_vault_platform *p) {
    return p->sd && p->sd->ops && p->sd->ops->read && p->sd->ops->write &&
        p->sd->ops->sync && p->sd->ops->block_count && p->sd->ops->is_present &&
        p->sd->ops->is_present(p->sd);
}
static bool state_valid(const fv_device_state *s) {
    uint8_t policy[16];
    if(s->status<FV_ENROLLMENT_EMPTY || s->status>FV_ENROLLMENT_DESTROYED)return false;
    if(s->status==FV_ENROLLMENT_EMPTY)return !s->attempts && !s->attempt_pending && !s->credential_generation;
    return s->credential_generation && fv_auth_policy_encode(&s->policy,policy) &&
        s->attempts<=s->policy.max_attempts && (!s->attempt_pending || s->attempts);
}
static int commit(const fv_vault_platform *p,fv_device_state *s) {
    uint64_t previous=s->sequence;
    if(previous==UINT64_MAX)return -1;
    s->sequence++;
    return p->authority.commit(p->authority.context,previous,s);
}
static fv_vault_result limit(const fv_vault_platform *p,fv_device_state *s) {
    if(s->status==FV_ENROLLMENT_ACTIVE) {
        s->status=s->policy.limit_action==FV_LIMIT_DESTROY?
            FV_ENROLLMENT_DESTROY_PENDING:FV_ENROLLMENT_LOCKED;
        s->attempt_pending=false;
        if(commit(p,s))return FV_VAULT_STATE;
    }
    if(s->status==FV_ENROLLMENT_DESTROY_PENDING) {
        if(!p->authority.destroy || p->authority.destroy(p->authority.context,s->token_slot))return FV_VAULT_STATE;
        s->status=FV_ENROLLMENT_DESTROYED;
        if(commit(p,s))return FV_VAULT_STATE;
    }
    return FV_VAULT_DENIED;
}
static fv_vault_result recover(const fv_vault_platform *p,fv_device_state *s) {
    if(!platform_valid(p))return FV_VAULT_INVALID;
    memset(s,0,sizeof(*s));
    if(p->authority.load(p->authority.context,s) || !state_valid(s))return FV_VAULT_STATE;
    if(s->status==FV_ENROLLMENT_DESTROY_PENDING ||
       (s->status==FV_ENROLLMENT_ACTIVE && s->attempts==s->policy.max_attempts))return limit(p,s);
    if(s->status==FV_ENROLLMENT_LOCKED || s->status==FV_ENROLLMENT_DESTROYED)return FV_VAULT_DENIED;
    if(s->attempt_pending) {
        s->attempt_pending=false;
        if(commit(p,s))return FV_VAULT_STATE;
    }
    return FV_VAULT_OK;
}
fv_vault_result fv_vault_recover(const fv_vault_platform *p) {
    fv_device_state s;
    return recover(p,&s);
}
void fv_vault_lock(fv_vault *v) {
    if(v)mbedtls_platform_zeroize(v,sizeof(*v));
}
static bool matches(const fv_envelope_config *c,const fv_device_state *s) {
    return c->credential_generation==s->credential_generation && c->token_slot==s->token_slot &&
        !memcmp(c->device_id,s->device_id,16) && !memcmp(c->volume.volume_id,s->volume_id,16) &&
        c->policy.max_attempts==s->policy.max_attempts && c->policy.limit_action==s->policy.limit_action;
}
static fv_vault_result select_header(const fv_vault_platform *p,const fv_device_state *s,
                                     uint8_t h[512],fv_envelope_config *c,unsigned *slot) {
    uint8_t hash[32];
    if(!media_valid(p))return FV_VAULT_IO;
    for(unsigned i=0;i<2;i++) {
        if(p->sd->ops->read(p->sd,i*8,1,h)!=FV_BLOCK_OK)continue;
        if(mbedtls_sha256(h,512,hash,0) || !fv_tag_equal(hash,s->header_hash))continue;
        if(!fv_envelope_parse(h,512,p->sd->ops->block_count(p->sd),p->kdf_limits,c) || !matches(c,s))continue;
        *slot=i;return FV_VAULT_OK;
    }
    return FV_VAULT_IO;
}
fv_vault_result fv_vault_credential_profile(const fv_vault_platform *p,uint16_t *profile){
    if(!profile)return FV_VAULT_INVALID;
    *profile=0;
    if(!platform_valid(p))return FV_VAULT_INVALID;
    fv_device_state state;
    if(p->authority.load(p->authority.context,&state) || !state_valid(&state) || state.status!=FV_ENROLLMENT_ACTIVE)return FV_VAULT_STATE;
    alignas(4) uint8_t header[512];fv_envelope_config config;unsigned slot;
    fv_vault_result r=select_header(p,&state,header,&config,&slot);
    if(!r)*profile=config.credential_profile;
    return r;
}
static int save_header(const fv_vault_platform *p,unsigned slot,const uint8_t h[512]) {
    alignas(4) uint8_t verify[512];
    return p->sd->ops->write(p->sd,slot*8,1,h)!=FV_BLOCK_OK ||
        p->sd->ops->sync(p->sd)!=FV_BLOCK_OK ||
        p->sd->ops->read(p->sd,slot*8,1,verify)!=FV_BLOCK_OK || memcmp(verify,h,512);
}
static int initialize_session(fv_vault *v,const fv_vault_platform *p,
                              const fv_envelope_config *c,const uint8_t vmk[32]) {
    fv_working_keys keys={0};fv_algorithm algorithms[4]={0};int r=-1;
    if(fv_derive_working_keys(vmk,&c->volume,&keys))goto done;
    for(unsigned i=0;i<c->volume.layer_count;i++)algorithms[i]=(fv_algorithm)c->volume.cipher_ids[i];
    if(fv_pipeline_init(&v->pipeline,algorithms,(const uint8_t (*)[64])keys.layers,c->volume.layer_count)!=FV_OK ||
       (c->volume.layout_version==2?fv_auth_open_bitmap:fv_auth_open)(&v->store,p->sd,FV_VOLUME_METADATA_BASE,c->volume.logical_blocks,
                    c->volume.volume_id,keys.integrity,NULL)!=FV_BLOCK_OK)goto done;
    v->config=*c;memcpy(v->vmk,vmk,32);v->platform=p;v->unlocked=true;r=0;
done:
    fv_working_keys_clear(&keys);
    if(r)fv_vault_lock(v);
    return r;
}
fv_vault_result fv_vault_create(const fv_vault_platform *p,uint64_t blocks,const uint16_t algorithms[4],
    uint8_t count,uint16_t profile,uint32_t iterations,fv_auth_policy policy,const uint8_t *secret,size_t n) {
    fv_device_state s;fv_vault_result r=recover(p,&s);
    if(r!=FV_VAULT_OK)return r;
    if(s.status!=FV_ENROLLMENT_EMPTY)return FV_VAULT_DENIED;
    if(!media_valid(p) || !p->random || !algorithms)return FV_VAULT_INVALID;
    fv_envelope_config c={.volume={.layout_version=2,.logical_blocks=blocks,.layer_count=count},
        .credential_generation=1,.credential_profile=profile,.iterations=iterations,
        .token_slot=s.token_slot,.policy=policy};
    memcpy(c.device_id,s.device_id,16);memcpy(c.volume.cipher_ids,algorithms,sizeof(c.volume.cipher_ids));
    alignas(4) uint8_t header[512];uint8_t vmk[32],binding[32];fv_vault temporary={0};
    r=FV_VAULT_INVALID;
    if(p->random(p->random_context,c.volume.volume_id,16) || p->random(p->random_context,vmk,32) ||
       p->authority.binding(p->authority.context,c.volume.volume_id,c.token_slot,binding) ||
       fv_envelope_seal(&c,p->sd->ops->block_count(p->sd),p->kdf_limits,binding,secret,n,vmk,
                        p->random,p->random_context,header) || initialize_session(&temporary,p,&c,vmk))goto done;
    r=FV_VAULT_IO;
    fv_block_result_t formatted=p->format_scratch?
        fv_auth_format_buffered(&temporary.store,p->format_scratch,p->format_sectors,p->format_progress,p->format_context):
        fv_auth_format(&temporary.store);
    if(formatted!=FV_BLOCK_OK || save_header(p,0,header) || save_header(p,1,header))goto done;
    s.status=FV_ENROLLMENT_ACTIVE;s.credential_generation=1;s.policy=policy;
    memcpy(s.volume_id,c.volume.volume_id,16);
    r=FV_VAULT_STATE;
    if(mbedtls_sha256(header,512,s.header_hash,0) || commit(p,&s))goto done;
    r=FV_VAULT_OK;
done:
    fv_vault_lock(&temporary);mbedtls_platform_zeroize(vmk,32);mbedtls_platform_zeroize(binding,32);
    return r;
}
fv_vault_result fv_vault_unlock(fv_vault *v,const fv_vault_platform *p,const uint8_t *secret,size_t n) {
    if(!v)return FV_VAULT_INVALID;
    fv_vault_lock(v);
    fv_device_state s;fv_vault_result r=recover(p,&s);
    if(r!=FV_VAULT_OK)return r;
    if(s.status!=FV_ENROLLMENT_ACTIVE)return FV_VAULT_DENIED;
    alignas(4) uint8_t header[512];fv_envelope_config c;unsigned slot;
    r=select_header(p,&s,header,&c,&slot);
    if(r!=FV_VAULT_OK)return r;
    /* Charge before any operation that can evaluate the user's credential. */
    s.attempts++;s.attempt_pending=true;
    if(commit(p,&s))return FV_VAULT_STATE;
    uint8_t binding[32]={0},vmk[32]={0};
    int failed=p->authority.binding(p->authority.context,s.volume_id,s.token_slot,binding) ||
        fv_envelope_open(header,p->sd->ops->block_count(p->sd),p->kdf_limits,binding,secret,n,vmk);
    mbedtls_platform_zeroize(binding,32);
    s.attempt_pending=false;
    if(!failed)s.attempts=0;
    r=FV_VAULT_STATE;
    if(commit(p,&s))goto done;
    if(failed) {
        r=s.attempts==s.policy.max_attempts?limit(p,&s):FV_VAULT_AUTH;
        goto done;
    }
    r=initialize_session(v,p,&c,vmk)?FV_VAULT_STATE:FV_VAULT_OK;
done:
    mbedtls_platform_zeroize(binding,32);mbedtls_platform_zeroize(vmk,32);
    if(r!=FV_VAULT_OK)fv_vault_lock(v);
    return r;
}
fv_vault_result fv_vault_change_credential(fv_vault *v,const fv_vault_platform *p,
    const uint8_t *current,size_t current_n,const uint8_t *replacement,size_t replacement_n,
    uint16_t profile,uint32_t iterations,fv_auth_policy policy,bool approved) {
    fv_vault_result r=fv_vault_unlock(v,p,current,current_n);
    if(r!=FV_VAULT_OK)return r;
    uint8_t binding[32]={0};alignas(4) uint8_t header[512];fv_device_state s;
    fv_envelope_config old;unsigned slot=0;
    r=FV_VAULT_STATE;
    if(p->authority.load(p->authority.context,&s) || !state_valid(&s) || s.status!=FV_ENROLLMENT_ACTIVE ||
       select_header(p,&s,header,&old,&slot)!=FV_VAULT_OK || s.credential_generation==UINT64_MAX)goto done;
    r=FV_VAULT_INVALID;
    if(!p->random || (!approved && (policy.max_attempts!=s.policy.max_attempts || policy.limit_action!=s.policy.limit_action)))goto done;
    fv_envelope_config c=v->config;
    c.credential_generation++;c.credential_profile=profile;c.iterations=iterations;c.policy=policy;
    if(p->authority.binding(p->authority.context,c.volume.volume_id,c.token_slot,binding) ||
       fv_envelope_seal(&c,p->sd->ops->block_count(p->sd),p->kdf_limits,binding,replacement,replacement_n,
                        v->vmk,p->random,p->random_context,header))goto done;
    r=FV_VAULT_IO;
    if(save_header(p,1-slot,header))goto done;
    s.credential_generation=c.credential_generation;s.policy=policy;
    r=FV_VAULT_STATE;
    if(mbedtls_sha256(header,512,s.header_hash,0) || commit(p,&s))goto done;
    /* Anchor committed: new credential is authoritative even if mirroring fails. */
    r=save_header(p,slot,header)?FV_VAULT_CHANGED_NEEDS_MIRROR:FV_VAULT_OK;
done:
    mbedtls_platform_zeroize(binding,32);fv_vault_lock(v);return r;
}
fv_vault_result fv_vault_repair_headers(fv_vault *v) {
    if(!v || !v->unlocked)return FV_VAULT_DENIED;
    const fv_vault_platform *p=v->platform;
    fv_device_state s;fv_envelope_config c;unsigned slot;
    alignas(4) uint8_t header[512];fv_vault_result r=FV_VAULT_STATE;
    if(p->authority.load(p->authority.context,&s) || !state_valid(&s) ||
       s.status!=FV_ENROLLMENT_ACTIVE || s.attempt_pending || !matches(&v->config,&s))goto done;
    r=select_header(p,&s,header,&c,&slot);
    if(r!=FV_VAULT_OK)goto done;
    if(save_header(p,1-slot,header))r=FV_VAULT_IO;
done:
    if(r!=FV_VAULT_OK)fv_vault_lock(v);
    return r;
}
static fv_block_result_t io(fv_vault *v,uint64_t lba,uint32_t count,uint8_t *buffer,bool write) {
    if(!buffer || ((uintptr_t)buffer&3) || !count || count>64)return FV_BLOCK_ERROR_INVALID_ARGUMENT;
    fv_block_result_t r=FV_BLOCK_ERROR_NOT_READY;
    if(!v || !v->unlocked)goto done;
    if(!media_valid(v->platform)){fv_vault_lock(v);goto done;}
    r=FV_BLOCK_ERROR_OUT_OF_RANGE;
    if(lba>=v->config.volume.logical_blocks || count>v->config.volume.logical_blocks-lba)goto done;
    uint64_t unset=0;
    if(!write) {
        r=fv_auth_read(&v->store,lba,count,buffer,&unset);
        if(r!=FV_BLOCK_OK)goto done;
    }
    for(uint32_t i=0;i<count;i++) {
        if(!write && (unset & (UINT64_C(1)<<i)))continue;
        uint8_t *sector=buffer+(size_t)i*512;
        fv_status status=write?fv_pipeline_encrypt(&v->pipeline,lba+i,sector,sector):
                               fv_pipeline_decrypt(&v->pipeline,lba+i,sector,sector);
        if(status!=FV_OK){r=FV_BLOCK_ERROR_IO;fv_vault_lock(v);goto done;}
    }
    r=FV_BLOCK_OK;
    if(write) {
        r=fv_auth_write(&v->store,lba,count,buffer);
        if(r==FV_BLOCK_OK)r=v->platform->sd->ops->sync(v->platform->sd);
    }
done:
    if((r!=FV_BLOCK_OK && r!=FV_BLOCK_ERROR_INVALID_ARGUMENT &&
        r!=FV_BLOCK_ERROR_OUT_OF_RANGE && r!=FV_BLOCK_ERROR_INTEGRITY) ||
       (v && v->unlocked && !v->store.ready))fv_vault_lock(v);
    if(write || r!=FV_BLOCK_OK)mbedtls_platform_zeroize(buffer,(size_t)count*512);
    return r;
}
fv_block_result_t fv_vault_read(fv_vault *v,uint64_t lba,uint32_t count,uint8_t *buffer) {return io(v,lba,count,buffer,false);}
fv_block_result_t fv_vault_write(fv_vault *v,uint64_t lba,uint32_t count,uint8_t *buffer) {return io(v,lba,count,buffer,true);}
