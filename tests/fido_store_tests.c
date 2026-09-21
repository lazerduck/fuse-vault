#include "fido_test_platform.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static uint8_t image[FV_FIDO_STORE_BYTES],recovered[FV_FIDO_STORE_BYTES];
static uint8_t backup[FV_TEST_CAPACITY*512u],after[FV_TEST_CAPACITY*512u];
static void media_copy(fv_fido_fixture *f,uint8_t *out){
    assert(!fflush(f->media));rewind(f->media);assert(fread(out,1,sizeof(backup),f->media)==sizeof(backup));
}
static void media_restore(fv_fido_fixture *f,const uint8_t *in){
    rewind(f->media);assert(fwrite(in,1,sizeof(backup),f->media)==sizeof(backup));assert(!fflush(f->media));
    fv_fido_fixture_checkpoint(f);
    fv_fido_fixture_io_reset(f);
}
static bool filled(const void *data,size_t size,uint8_t value){
    const uint8_t *p=data;while(size--)if(*p++!=value)return false;return true;
}
static void bounds_unchanged(fv_fido_fixture *f){
    media_copy(f,after);assert(!memcmp(backup,after,16u*512));
    assert(!memcmp(backup+2064u*512,after+2064u*512,sizeof(backup)-2064u*512));
}
static void lifecycle(const uint16_t alg[4],unsigned layers){
    fv_fido_fixture f;fv_fido_fixture_init(&f,alg,layers);fv_fido_store s={0};
    alignas(4) uint8_t data[512];memset(data,0x91,sizeof(data));
    assert(fv_vault_write(&f.vault,7,1,data)==FV_BLOCK_OK);media_copy(&f,backup);
    assert(fv_fido_store_open(&s,&f.vault,image)==FV_FIDO_STORE_CORRUPT);
    assert(filled(image,sizeof(image),0));
    unsigned writes=f.writes;
    uint64_t authority_sequence=f.authority.sequence;
    assert(fv_fido_store_initialize(&s,&f.vault,false,image)==FV_FIDO_STORE_INVALID);
    assert(f.writes==writes);
    assert(fv_fido_store_initialize(&s,&f.vault,true,image)==FV_FIDO_STORE_OK);
    assert(filled(image,sizeof(image),255));
    memset(image,0x39,sizeof(image));
    assert(fv_fido_store_commit(&s,image)==FV_FIDO_STORE_OK);
    assert(f.authority.sequence==authority_sequence); /* no flash commit per snapshot */
    assert(filled(image,sizeof(image),0x39));bounds_unchanged(&f);
    uint8_t key[32],same[32];assert(fv_fido_store_engine_key(&s,key)==FV_FIDO_STORE_OK);
    assert(memcmp(key,f.vault.vmk,32));
    fv_working_keys usb;assert(!fv_derive_working_keys(f.vault.vmk,&f.vault.config.volume,&usb));
    assert(memcmp(key,usb.integrity,32) && memcmp(key,usb.layers[0],32));fv_working_keys_clear(&usb);
    fv_fido_store_close(&s);fv_vault_lock(&f.vault);
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_LOCKED);
    assert(filled(recovered,sizeof(recovered),0));
    fv_fido_fixture_unlock(&f);assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_OK);
    assert(!memcmp(image,recovered,sizeof(image)));
    fv_fido_store_close(&s);fv_fido_fixture_change(&f);
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_OK);
    assert(fv_fido_store_engine_key(&s,same)==FV_FIDO_STORE_OK && !memcmp(key,same,32));
    assert(!memcmp(image,recovered,sizeof(image)));
    assert(fv_vault_read(&f.vault,7,1,data)==FV_BLOCK_OK && filled(data,sizeof(data),0x91));
    f.authority.status=FV_ENROLLMENT_DESTROY_PENDING;
    assert(fv_fido_store_engine_key(&s,same)==FV_FIDO_STORE_LOCKED && filled(same,32,0));
    f.authority.status=FV_ENROLLMENT_ACTIVE;
    fv_vault_lock(&f.vault);
    assert(fv_fido_store_engine_key(&s,same)==FV_FIDO_STORE_LOCKED && filled(same,32,0));
    assert(fv_fido_store_commit(&s,image)==FV_FIDO_STORE_LOCKED && !s.ready);
    fv_fido_store_close(&s);fv_fido_fixture_close(&f);
}
static void recovery(void){
    const uint16_t alg[4]={1,2};fv_fido_fixture f;fv_fido_fixture_init(&f,alg,2);fv_fido_store s={0};
    assert(fv_fido_store_initialize(&s,&f.vault,true,image)==FV_FIDO_STORE_OK);
    memset(image,0x19,sizeof(image));assert(fv_fido_store_commit(&s,image)==FV_FIDO_STORE_OK);
    media_copy(&f,backup);memset(image,0x28,sizeof(image));
    fv_fido_fixture_io_reset(&f);
    assert(fv_fido_store_commit(&s,image)==FV_FIDO_STORE_OK);
    unsigned writes=f.writes,syncs=f.syncs,reads=f.reads;
    /* Every write call, partial writes including metadata and manifest, every
     * synchronization point and every readback/manifest read failure. */
    const unsigned tears[]={0,17,512,4095};
    unsigned cases=0;
    for(unsigned kind=0;kind<3;kind++){
        unsigned total=kind==0?writes:kind==1?syncs:reads;
        for(unsigned cut=1;cut<=total;cut++)for(unsigned t=0;t<(kind==0?4u:1u);t++)for(unsigned drop=0;drop<2;drop++){
            media_restore(&f,backup);
            assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_OK);
            fv_fido_fixture_io_reset(&f);
            if(kind==0){f.fail_write=cut;f.tear_bytes=tears[t];}
            if(kind==1)f.fail_sync=cut;
            if(kind==2)f.fail_read=cut;
            assert(fv_fido_store_commit(&s,image)!=FV_FIDO_STORE_OK && !s.ready);
            fv_fido_fixture_power_cut(&f,drop!=0);
            assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_OK);
            assert(filled(recovered,sizeof(recovered),0x19) || filled(recovered,sizeof(recovered),0x28));
            bounds_unchanged(&f);cases++;
        }
    }
    printf("Snapshot interruption cases: %u (%u writes, %u syncs, %u reads)\n",cases,writes,syncs,reads);
    const unsigned corrupt_at[]={4,5,writes}; /* first payload, tags, publication */
    for(unsigned i=0;i<3;i++){
        media_restore(&f,backup);
        assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_OK);
        fv_fido_fixture_io_reset(&f);f.corrupt_write=corrupt_at[i];
        assert(fv_fido_store_commit(&s,image)!=FV_FIDO_STORE_OK);
        fv_fido_fixture_io_reset(&f);
        assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_OK);
        assert(filled(recovered,sizeof(recovered),0x19));
    }
    media_restore(&f,backup);
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_OK);
    unsigned open_reads=f.reads;
    for(unsigned cut=1;cut<=open_reads;cut++){
        fv_fido_fixture_io_reset(&f);f.fail_read=cut;
        memset(recovered,0x71,sizeof(recovered));
        assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_IO);
        assert(filled(recovered,sizeof(recovered),0) && f.writes==0 && !s.ready);
    }
    /* Valid old storage is deliberately accepted; no internal anchor changed. */
    media_restore(&f,backup);
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_OK);
    assert(s.generation==2 && filled(recovered,sizeof(recovered),0x19));
    assert(fv_fido_store_commit(&s,image)==FV_FIDO_STORE_OK);
    assert(fv_fido_store_commit(&s,image)==FV_FIDO_STORE_OK); /* reuse original bank */
    media_copy(&f,after);
    /* Replay valid old ciphertext AND sector tags beneath a new valid manifest.
     * Sector authentication passes; the whole-snapshot digest must reject it. */
    memcpy(after+1041u*512,backup+1041u*512,137u*512);
    media_restore(&f,after);
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_CORRUPT);
    media_restore(&f,backup);
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_OK);
    fv_fido_store stale=s;
    assert(fv_fido_store_commit(&s,image)==FV_FIDO_STORE_OK);
    assert(fv_fido_store_commit(&stale,image)==FV_FIDO_STORE_STALE && !stale.ready);
    /* Latest committed corruption must fail closed, not resurrect the older bank. */
    unsigned payload=(16u+s.bank*1024u+10u)*512u;
    media_copy(&f,after);after[payload+7]^=1;media_restore(&f,after);
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_CORRUPT);
    assert(filled(recovered,sizeof(recovered),0));
    media_restore(&f,backup);
    f.vault.vmk[0]^=1;
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_CORRUPT);
    f.vault.vmk[0]^=1;
    f.vault.config.volume.volume_id[0]^=1;
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_LOCKED);
    f.authority.volume_id[0]^=1;
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_CORRUPT);
    f.authority.volume_id[0]^=1;
    f.vault.config.volume.volume_id[0]^=1;
    /* A valid snapshot on the wrong bank cannot be substituted. */
    media_copy(&f,after);
    memcpy(after+16u*512,after+1040u*512,1024u*512);
    memset(after+1040u*512,0,512);media_restore(&f,after);
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_CORRUPT);
    media_restore(&f,backup);
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_OK);
    f.present=false;
    assert(fv_fido_store_commit(&s,image)==FV_FIDO_STORE_LOCKED && !s.ready);f.present=true;
    /* Destroy authority using the real vault's final-attempt path. */
    fv_vault_lock(&f.vault);
    for(unsigned i=0;i<10;i++)assert(fv_vault_unlock(&f.vault,&f.platform,(const uint8_t*)"wrong",5)!=FV_VAULT_OK);
    assert(f.destroyed && f.authority.status==FV_ENROLLMENT_DESTROYED);
    media_restore(&f,backup);
    assert(fv_vault_unlock(&f.vault,&f.platform,(const uint8_t*)"fixture secret",14)==FV_VAULT_DENIED);
    assert(fv_fido_store_open(&s,&f.vault,recovered)==FV_FIDO_STORE_LOCKED);
    fv_fido_store_close(&s);fv_fido_fixture_close(&f);
}
static void initialization_failures(void){
    const uint16_t alg[4]={1};fv_fido_fixture f;fv_fido_fixture_init(&f,alg,1);fv_fido_store s={0};
    media_copy(&f,backup);assert(fv_fido_store_initialize(&s,&f.vault,true,image)==FV_FIDO_STORE_OK);
    unsigned writes=f.writes,syncs=f.syncs;
    for(unsigned kind=0;kind<2;kind++)for(unsigned cut=1;cut<=(kind?syncs:writes);cut++)for(unsigned drop=0;drop<2;drop++){
        media_restore(&f,backup);
        if(kind)f.fail_sync=cut;else {f.fail_write=cut;f.tear_bytes=17;}
        assert(fv_fido_store_initialize(&s,&f.vault,true,image)!=FV_FIDO_STORE_OK);
        assert(filled(image,sizeof(image),0) && !s.ready);
        fv_fido_fixture_power_cut(&f,drop!=0);
        fv_fido_store_result r=fv_fido_store_open(&s,&f.vault,recovered);
        assert(r==FV_FIDO_STORE_CORRUPT || (r==FV_FIDO_STORE_OK && filled(recovered,sizeof(recovered),255)));
        bounds_unchanged(&f);
    }
    fv_fido_store_close(&s);fv_fido_fixture_close(&f);
}
static void reverify(void){
    fv_fido_fixture f;const uint16_t alg[4]={1};fv_fido_fixture_init(&f,alg,1);
    uint8_t vmk[32];memcpy(vmk,f.vault.vmk,32);uint64_t generation=f.vault.config.credential_generation;
    const uint8_t secret[]="fixture secret",wrong[]="wrong";
    assert(fv_vault_reverify(&f.vault,secret,sizeof(secret)-1)==FV_VAULT_OK);
    assert(f.vault.unlocked && !memcmp(vmk,f.vault.vmk,32) && f.vault.config.credential_generation==generation);
    assert(fv_vault_reverify(&f.vault,wrong,sizeof(wrong)-1)!=FV_VAULT_OK);
    assert(!f.vault.unlocked && f.authority.attempts==1);
    assert(fv_vault_reverify(&f.vault,secret,sizeof(secret)-1)==FV_VAULT_DENIED);
    assert(f.authority.attempts==1);fv_fido_fixture_close(&f);
}
int main(void){
    reverify();
    const uint16_t algs[][4]={{1,0,0,0},{2,0,0,0},{1,2,1,2}};
    for(unsigned i=0;i<3;i++)lifecycle(algs[i],i==2?4:1);
    recovery();initialization_failures();puts("FIDO encrypted store lifecycle/failure tests passed");
}
