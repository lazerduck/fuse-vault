#include "fido_test_platform.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static const uint8_t secret[]="fixture secret",replacement[]="replacement fixture secret";
static fv_block_result_t read_blocks(fv_block_device_t *d,uint64_t lba,uint32_t n,uint8_t *out){
    fv_fido_fixture *f=d->context;
    if(!f->present || lba>=FV_TEST_CAPACITY || n>FV_TEST_CAPACITY-lba || ++f->reads==f->fail_read)return FV_BLOCK_ERROR_IO;
    return fseek(f->media,(long)(lba*512),SEEK_SET) || fread(out,512,n,f->media)!=n?FV_BLOCK_ERROR_IO:FV_BLOCK_OK;
}
static fv_block_result_t write_blocks(fv_block_device_t *d,uint64_t lba,uint32_t n,const uint8_t *in){
    fv_fido_fixture *f=d->context;
    if(!f->present || lba>=FV_TEST_CAPACITY || n>FV_TEST_CAPACITY-lba)return FV_BLOCK_ERROR_IO;
    f->last_lba=lba;
    if(fseek(f->media,(long)(lba*512),SEEK_SET))return FV_BLOCK_ERROR_IO;
    if(++f->writes==f->fail_write){
        size_t bytes=f->tear_bytes;if(bytes>(size_t)n*512)bytes=(size_t)n*512;
        assert(fwrite(in,1,bytes,f->media)==bytes);assert(!fflush(f->media));return FV_BLOCK_ERROR_IO;
    }
    if(fwrite(in,512,n,f->media)!=n)return FV_BLOCK_ERROR_IO;
    if(f->writes==f->corrupt_write){
        assert(!fseek(f->media,(long)(lba*512+7),SEEK_SET));assert(fputc(in[7]^1,f->media)!=EOF);
    }
    return FV_BLOCK_OK;
}
static fv_block_result_t sync_blocks(fv_block_device_t *d){
    fv_fido_fixture *f=d->context;
    if(fflush(f->media) || ++f->syncs==f->fail_sync)return FV_BLOCK_ERROR_IO;
    fv_fido_fixture_checkpoint(f);
    return FV_BLOCK_OK;
}
static uint64_t capacity(const fv_block_device_t *d){(void)d;return FV_TEST_CAPACITY;}
static bool present(const fv_block_device_t *d){return ((fv_fido_fixture*)d->context)->present;}
static const fv_block_device_ops_t ops={read_blocks,write_blocks,sync_blocks,capacity,present};
static int load(void *ctx,fv_device_state *s){*s=((fv_fido_fixture*)ctx)->authority;return 0;}
static int commit(void *ctx,uint64_t previous,const fv_device_state *s){
    fv_fido_fixture *f=ctx;assert(f->authority.sequence==previous && s->sequence==previous+1);f->authority=*s;return 0;
}
static int binding(void *ctx,const uint8_t id[16],uint32_t slot,uint8_t out[32]){
    fv_fido_fixture *f=ctx;uint8_t root[32]={1},token[32]={2};
    return f->destroyed?-1:fv_vault_binding(root,token,slot,id,out);
}
static int destroy(void *ctx,uint32_t slot){(void)slot;((fv_fido_fixture*)ctx)->destroyed=true;return 0;}
static int random_bytes(void *ctx,uint8_t *out,size_t n){
    fv_fido_fixture *f=ctx;for(size_t i=0;i<n;i++)out[i]=(uint8_t)(++f->entropy);return 0;
}
void fv_fido_fixture_io_reset(fv_fido_fixture *f){
    f->writes=f->reads=f->syncs=f->fail_write=f->fail_sync=f->fail_read=f->tear_bytes=f->corrupt_write=0;
}
void fv_fido_fixture_checkpoint(fv_fido_fixture *f){
    assert(!fflush(f->media));rewind(f->media);
    assert(fread(f->durable,512,FV_TEST_CAPACITY,f->media)==FV_TEST_CAPACITY);
}
void fv_fido_fixture_power_cut(fv_fido_fixture *f,bool discard_unsynchronized){
    if(discard_unsynchronized){
        rewind(f->media);assert(fwrite(f->durable,512,FV_TEST_CAPACITY,f->media)==FV_TEST_CAPACITY);
        assert(!fflush(f->media));
    }
    fv_fido_fixture_io_reset(f);
}
void fv_fido_fixture_unlock(fv_fido_fixture *f){
    const uint8_t *password=f->credential_changed?replacement:secret;
    size_t n=f->credential_changed?sizeof(replacement)-1:sizeof(secret)-1;
    assert(fv_vault_unlock(&f->vault,&f->platform,password,n)==FV_VAULT_OK);
}
void fv_fido_fixture_change(fv_fido_fixture *f){
    assert(fv_vault_change_credential(&f->vault,&f->platform,secret,sizeof(secret)-1,replacement,sizeof(replacement)-1,
        1,2,fv_auth_policy_default(),false)==FV_VAULT_OK);
    assert(!f->vault.unlocked);
    f->credential_changed=true;fv_fido_fixture_unlock(f);
}
void fv_fido_fixture_init(fv_fido_fixture *f,const uint16_t algorithms[4],unsigned layers){
    memset(f,0,sizeof(*f));f->media=tmpfile();assert(f->media);f->present=true;
    f->durable=malloc(FV_TEST_CAPACITY*512u);assert(f->durable);
    uint8_t sector[512];memset(sector,0xa5,sizeof(sector));
    for(unsigned i=0;i<FV_TEST_CAPACITY;i++)assert(fwrite(sector,512,1,f->media)==1);
    assert(!fflush(f->media));f->device=(fv_block_device_t){&ops,f};
    f->authority=(fv_device_state){.status=FV_ENROLLMENT_EMPTY,.device_id={3}};
    f->platform=(fv_vault_platform){.sd=&f->device,.authority={f,load,commit,binding,destroy},
        .random=random_bytes,.random_context=f,.kdf_limits={1,10}};
    assert(fv_vault_create(&f->platform,64,algorithms,(uint8_t)layers,1,2,fv_auth_policy_default(),secret,sizeof(secret)-1)==FV_VAULT_OK);
    fv_fido_fixture_unlock(f);fv_fido_fixture_io_reset(f);
}
void fv_fido_fixture_close(fv_fido_fixture *f){fv_vault_lock(&f->vault);assert(!fclose(f->media));free(f->durable);}
