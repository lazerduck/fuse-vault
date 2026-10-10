#include "fido_test_platform.h"
#include "fuse_vault/fido_journal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
/* Test-only access to the legacy writer constructs both historical formats. */
#include "../../shared/fido/store.c"

static fv_fido_fixture f;
static fv_fido_journal j;
static uint8_t *baseline;
static void save(void){fv_fido_fixture_checkpoint(&f);memcpy(baseline,f.durable,FV_TEST_CAPACITY*512);}
static void restore(void){
    fv_fido_journal_close(&j);rewind(f.media);assert(fwrite(baseline,512,FV_TEST_CAPACITY,f.media)==FV_TEST_CAPACITY);
    assert(!fflush(f.media));memcpy(f.durable,baseline,FV_TEST_CAPACITY*512);fv_fido_fixture_io_reset(&f);
}
static void outside(void){
    uint8_t p[512];
    for(unsigned i=0;i<FV_TEST_CAPACITY;i++)if(i<16 || i>=2064){
        assert(f.device.ops->read(&f.device,i,1,p)==FV_BLOCK_OK);assert(!memcmp(p,baseline+i*512,512));
    }
}
static void recover(bool lost){fv_fido_journal_close(&j);fv_fido_fixture_power_cut(&f,lost);assert(!fv_fido_journal_open(&j,&f.vault));}
static void check_pair(void){
    uint8_t a[512],b[512];assert(fv_fido_journal_read(&j,0,a,512));assert(fv_fido_journal_read(&j,20*512,b,512));
    assert((a[0]==0xff && b[0]==0xff) || (a[0]==0x12 && b[0]==0x34));
    for(unsigned i=0;i<512;i++){assert(a[i]==a[0]);assert(b[i]==b[0]);}
}
static bool transaction(void){
    uint8_t p[512];memset(p,0x12,512);if(!fv_fido_journal_write(&j,0,p,512))return false;
    memset(p,0x34,512);return fv_fido_journal_write(&j,20*512,p,512) && fv_fido_journal_commit(&j);
}
static void journal_faults(void){
    assert(!fv_fido_journal_initialize(&j,&f.vault,true));save();fv_fido_fixture_io_reset(&f);
    assert(transaction());unsigned writes=f.writes,syncs=f.syncs,reads=f.reads;
    for(unsigned lost=0;lost<2;lost++)for(unsigned mode=0;mode<3;mode++){
        unsigned count=mode==0?writes:mode==1?syncs:reads;
        for(unsigned n=1;n<=count;n++){
            restore();assert(!fv_fido_journal_open(&j,&f.vault));fv_fido_fixture_io_reset(&f);
            if(mode==0){f.fail_write=n;f.tear_bytes=(n%3==0)?511:(n%3==1)?1:257;}
            else if(mode==1)f.fail_sync=n;else f.fail_read=n;
            (void)transaction();recover(lost);check_pair();outside();
        }
    }
    printf("Journal: every write/sync/read interruption, torn sectors and lost unsynchronized writes (%u/%u/%u)\n",writes,syncs,reads);
    restore();assert(!fv_fido_journal_open(&j,&f.vault));
    uint8_t p[512];memset(p,0x77,512);unsigned k=0;
    while(k<200 && fv_fido_journal_write(&j,k*512,p,512))k++;
    assert(k<200);recover(false);assert(fv_fido_journal_read(&j,0,p,512) && p[0]==255);
    puts("Journal overflow leaves home unchanged");
}
static void reset_faults(void){
    restore();assert(!fv_fido_journal_open(&j,&f.vault));assert(transaction());save();fv_fido_fixture_io_reset(&f);
    assert(fv_fido_journal_reset(&j,1));unsigned writes=f.writes,syncs=f.syncs;
    for(unsigned mode=0;mode<2;mode++)for(unsigned n=1;n<=(mode?syncs:writes);n++){
        restore();assert(!fv_fido_journal_open(&j,&f.vault));fv_fido_fixture_io_reset(&f);
        if(mode)f.fail_sync=n;else {f.fail_write=n;f.tear_bytes=n%3?257:4095;}
        (void)fv_fido_journal_reset(&j,1);recover(n%2);
        uint8_t p[512];assert(fv_fido_journal_read(&j,0,p,512));assert(p[0]==0x12 || p[0]==255);
        if(p[0]==255){
            size_t data=FV_FIDO_DISK_BYTES-12-16384-8;
            assert(fv_fido_journal_read(&j,data-2,p,2));assert(p[0]==1 && p[1]==1);
        }
        outside();
    }
    printf("Destructive reset: every write/sync interruption (%u/%u), preserved policy\n",writes,syncs);
}
static void migration_faults(unsigned bytes){
    fv_fido_journal_close(&j);
    /* Restore a card with no new-format controls and a valid legacy 128K image. */
    uint8_t p[512];memset(p,0xa5,512);
    for(unsigned i=16;i<2064;i++)assert(f.device.ops->write(&f.device,i,1,p)==FV_BLOCK_OK);
    uint8_t *image=malloc(FV_FIDO_STORE_BYTES);assert(image);fv_fido_store legacy={0};
    assert(!fv_fido_store_initialize(&legacy,&f.vault,true,image));
    /* Empty filesystem roots with a real record whose list links must relocate. */
    unsigned rom=bytes-12,data=rom-16384-8,record=data-16;
    memset(image+rom,0,12);memset(image+data,0,8);memset(image+record,0,16);
    uint32_t a=4096+record,b=4096+data;memcpy(image+data,&a,4);memcpy(image+record+4,&b,4);
    image[record+8]=0x23;image[record+9]=0x11;image[record+10]=4;memcpy(image+record+12,"test",4);
    snapshot source={.generation=2,.bank=1,.image_bytes=bytes};
    assert(!write_snapshot(&f.vault,&source,image));fv_fido_store_close(&legacy);free(image);save();fv_fido_fixture_io_reset(&f);
    assert(!fv_fido_journal_open(&j,&f.vault));unsigned writes=f.writes,syncs=f.syncs;
    for(unsigned mode=0;mode<2;mode++)for(unsigned n=1;n<=(mode?syncs:writes);n++){
        restore();fv_fido_fixture_io_reset(&f);
        if(mode)f.fail_sync=n;else{f.fail_write=n;f.tear_bytes=n%3?257:4095;}
        (void)fv_fido_journal_open(&j,&f.vault);recover(n%2);
        unsigned target=FV_FIDO_DISK_BYTES-12-16384-8;
        assert(fv_fido_journal_read(&j,target,p,4));memcpy(&a,p,4);assert(a==4096+target-16);
        assert(fv_fido_journal_read(&j,target-16,p,16));memcpy(&b,p+4,4);assert(b==4096+target && !memcmp(p+12,"test",4));
        outside();
    }
    printf("Startup migration %uK: every write/sync interruption (%u/%u), relocated links and payload\n",bytes/1024,writes,syncs);
}
static void corruption_and_recovery(void){
    assert(!fv_fido_journal_initialize(&j,&f.vault,true));save();
    uint8_t p[512];memset(p,0x63,512);
    assert(fv_fido_journal_write(&j,0,p,512));fv_fido_fixture_io_reset(&f);
    f.fail_write=2; /* READY is durable; interrupt the first home write. */
    assert(!fv_fido_journal_commit(&j));fv_fido_fixture_io_reset(&f);
    save();fv_fido_journal_close(&j);
    assert(!fv_fido_journal_open(&j,&f.vault));unsigned writes=f.writes,syncs=f.syncs;
    for(unsigned mode=0;mode<2;mode++)for(unsigned n=1;n<=(mode?syncs:writes);n++){
        restore();if(mode)f.fail_sync=n;else{f.fail_write=n;f.tear_bytes=31;}
        (void)fv_fido_journal_open(&j,&f.vault);recover(n%2);
        assert(fv_fido_journal_read(&j,0,p,512) && p[0]==0x63);
    }
    restore();assert(f.device.ops->read(&f.device,16+J_START+1,1,p)==FV_BLOCK_OK);p[0]^=1;
    assert(f.device.ops->write(&f.device,16+J_START+1,1,p)==FV_BLOCK_OK);
    fv_fido_fixture_io_reset(&f);assert(fv_fido_journal_open(&j,&f.vault)!=0 && f.writes==0);
    restore();recover(false);save();fv_fido_journal_close(&j);
    assert(f.device.ops->read(&f.device,16+J_META,1,p)==FV_BLOCK_OK);p[9]^=1;
    assert(f.device.ops->write(&f.device,16+J_META,1,p)==FV_BLOCK_OK);
    assert(!fv_fido_journal_open(&j,&f.vault));memset(p,99,512);
    assert(!fv_fido_journal_read(&j,0,p,512));for(unsigned i=0;i<512;i++)assert(!p[i]);
    restore();f.vault.vmk[0]^=1;assert(fv_fido_journal_open(&j,&f.vault)!=0 && f.writes==0);f.vault.vmk[0]^=1;
    restore();assert(!fv_fido_journal_open(&j,&f.vault));fv_vault_lock(&f.vault);
    assert(!fv_fido_journal_read(&j,0,p,512));fv_fido_fixture_unlock(&f);
    assert(!fv_fido_journal_open(&j,&f.vault));fv_fido_fixture_io_reset(&f);
    f.corrupt_write=2+(J_META+FV_TAG_CACHE_BLOCKS-1)/FV_TAG_CACHE_BLOCKS;
    assert(!fv_fido_journal_reset(&j,-1));recover(false);
    assert(fv_fido_journal_read(&j,0,p,512) && p[0]==255);
    puts("Repeated recovery interruptions, committed-journal corruption, home corruption, wrong VMK and lock passed");
}

int main(void){
    const uint16_t algorithms[4]={1};fv_fido_fixture_init(&f,algorithms,1);
    baseline=malloc(FV_TEST_CAPACITY*512);assert(baseline);
    journal_faults();reset_faults();migration_faults(131072);migration_faults(65536);corruption_and_recovery();
    fv_fido_journal_close(&j);fv_fido_fixture_close(&f);free(baseline);
}
