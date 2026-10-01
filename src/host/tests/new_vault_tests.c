/* Reuse the file-backed authority and failure-injection fixture. */
#define main vault_regression_main
#include "vault_tests.c"
#undef main
#include "vault_filesystem.h"
#include "fuse_vault/fido_store.h"
#include "ff.h"
#include "diskio.h"

static alignas(4) uint8_t workspace[32768];
static uint8_t fido_image[FV_FIDO_STORE_BYTES];
static fixture *mounted;
static unsigned setup_calls, failure_phase;
static int setup_contents(fv_vault *v,void *context){
    fixture *f=context;fv_device_state s;CHECK(!load(f,&s));
    CHECK(s.status==FV_ENROLLMENT_EMPTY);++setup_calls;
    if(failure_phase==1)f->fail_sync=f->syncs+1;
    int r=fv_vault_format_fat32(v,workspace,sizeof(workspace));
    if(r)return r;
    if(failure_phase==2)f->fail_sync=f->syncs+1;
    r=fv_fido_store_prepare_new(v,fido_image);
    CHECK(zero(fido_image,sizeof(fido_image)));
    return failure_phase==3?-1:r; /* Interruption before activation. */
}
static void new_fixture(fixture *f){
    init(f);f->sectors=300000; /* Sparse ~146 MiB card. */
    CHECK(!fseek(f->sd,(long)(f->sectors*512-1),SEEK_SET));CHECK(fputc(0,f->sd)!=EOF);CHECK(!fflush(f->sd));
    f->platform.format_scratch=workspace;f->platform.format_sectors=64;
    f->platform.initialize_contents=setup_contents;f->platform.initialize_context=f;
}
static fv_vault_result create_ready(fixture *f){
    const uint16_t ciphers[4]={1,2};
    return fv_vault_create(&f->platform,262144,ciphers,2,1,3,
        fv_auth_policy_default(),password,sizeof(password)-1);
}
/* Unwrapped callbacks are a host-only view of the unlocked encrypted disk.
 * The formatter must intercept them: no raw-card fallback is allowed. */
DSTATUS disk_initialize(BYTE drive){CHECK(mounted && !drive);return mounted->session.unlocked?0:STA_NOINIT;}
DSTATUS disk_status(BYTE drive){return disk_initialize(drive);}
DRESULT disk_read(BYTE drive,BYTE *out,LBA_t lba,UINT count){
    CHECK(mounted && !drive);
    while(count--){alignas(4) uint8_t sector[512];
        if(fv_vault_read(&mounted->session,lba++,1,sector)!=FV_BLOCK_OK)return RES_ERROR;
        memcpy(out,sector,512);out+=512;
    }return RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *in,LBA_t lba,UINT count){
    CHECK(mounted && !drive);
    while(count--){alignas(4) uint8_t sector[512];memcpy(sector,in,512);
        if(fv_vault_write(&mounted->session,lba++,1,sector)!=FV_BLOCK_OK)return RES_ERROR;
        in+=512;
    }return RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *out){
    CHECK(mounted && !drive);
    if(command==CTRL_SYNC)return sync_device(&mounted->device)==FV_BLOCK_OK?RES_OK:RES_ERROR;
    if(command==GET_SECTOR_COUNT){*(LBA_t *)out=mounted->session.config.volume.logical_blocks;return RES_OK;}
    return RES_PARERR;
}
DWORD get_fattime(void){return (DWORD)(2026-1980)<<25 | 9u<<21 | 29u<<16;}

static void export_partition(fixture *f,const char *path){
    alignas(4) uint8_t sector[512];CHECK(fv_vault_read(&f->session,0,1,sector)==FV_BLOCK_OK);
    CHECK(sector[510]==0x55 && sector[511]==0xaa);
    CHECK(sector[450]==0x0c); /* MBR FAT32 LBA, not a superfloppy. */
    uint32_t start=0,count=0;
    for(unsigned i=0;i<4;i++){start|=(uint32_t)sector[454+i]<<(8*i);count|=(uint32_t)sector[458+i]<<(8*i);}
    CHECK(start && count && (uint64_t)start+count<=262144);
    if(!path)return;
    FILE *out=fopen(path,"wb");CHECK(out);
    for(uint32_t i=0;i<count;i++){
        CHECK(fv_vault_read(&f->session,start+i,1,sector)==FV_BLOCK_OK);
        if(zero(sector,512))CHECK(!fseek(out,512,SEEK_CUR));
        else CHECK(fwrite(sector,512,1,out)==1);
    }
    CHECK(!fseek(out,(long)count*512-1,SEEK_SET));CHECK(fputc(0,out)!=EOF);CHECK(!fclose(out));
}
int main(int argc,char **argv){
    fixture f;new_fixture(&f);CHECK(create_ready(&f)==FV_VAULT_OK);CHECK(setup_calls==1);
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);
    fv_fido_store store={0};CHECK(fv_fido_store_open(&store,&f.session,fido_image)==FV_FIDO_STORE_OK);
    CHECK(fido_image[0]==255);memset(fido_image,0x39,sizeof(fido_image));
    CHECK(fv_fido_store_commit(&store,fido_image)==FV_FIDO_STORE_OK);fv_fido_store_close(&store);
    /* Neither automatic initializer is allowed to erase an active vault. */
    CHECK(fv_vault_format_fat32(&f.session,workspace,sizeof(workspace))!=0);
    CHECK(fv_fido_store_prepare_new(&f.session,fido_image)==FV_FIDO_STORE_LOCKED);
    CHECK(create_ready(&f)==FV_VAULT_DENIED && setup_calls==1);
    mounted=&f;FATFS fs;FIL file;UINT n;
    CHECK(f_mount(&fs,"0:",1)==FR_OK);CHECK(fs.fs_type==FS_FAT32 && fs.n_fats==2);
    CHECK(f_open(&file,"0:/FIRST.TXT",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    CHECK(f_write(&file,"Ready to use",12,&n)==FR_OK && n==12);CHECK(f_close(&file)==FR_OK);
    CHECK(f_mount(NULL,"0:",0)==FR_OK);fv_vault_lock(&f.session);
    CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK && setup_calls==1);
    CHECK(f_mount(&fs,"0:",1)==FR_OK);CHECK(f_open(&file,"0:/FIRST.TXT",FA_READ)==FR_OK);
    char text[16]={0};CHECK(f_read(&file,text,sizeof(text),&n)==FR_OK && n==12);
    CHECK(!memcmp(text,"Ready to use",12));CHECK(f_close(&file)==FR_OK);CHECK(f_mount(NULL,"0:",0)==FR_OK);
    CHECK(fv_fido_store_open(&store,&f.session,fido_image)==FV_FIDO_STORE_OK && fido_image[0]==0x39);
    fv_fido_store_close(&store);export_partition(&f,argc>1?argv[1]:NULL);mounted=NULL;finish(&f);
    /* Both filesystem and FIDO write/sync failures leave setup retryable. */
    for(failure_phase=1;failure_phase<=3;failure_phase++){
        new_fixture(&f);CHECK(create_ready(&f)==FV_VAULT_IO);
        fv_device_state state;CHECK(!load(&f,&state));CHECK(state.status==FV_ENROLLMENT_EMPTY);
        CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_DENIED);
        f.fail_sync=0;unsigned phase=failure_phase;failure_phase=0;
        CHECK(create_ready(&f)==FV_VAULT_OK);failure_phase=phase;
        CHECK(unlock(&f,password,sizeof(password)-1)==FV_VAULT_OK);
        CHECK(fv_fido_store_open(&store,&f.session,fido_image)==FV_FIDO_STORE_OK);
        fv_fido_store_close(&store);finish(&f);
    }
    /* Too-small media fails without ever making an unusable vault ACTIVE. */
    failure_phase=0;init(&f);f.platform.initialize_contents=setup_contents;f.platform.initialize_context=&f;
    const uint16_t small_ciphers[4]={1};
    CHECK(fv_vault_create(&f.platform,64,small_ciphers,1,1,3,
        fv_auth_policy_default(),password,sizeof(password)-1)==FV_VAULT_IO);
    fv_device_state state;CHECK(!load(&f,&state) && state.status==FV_ENROLLMENT_EMPTY);finish(&f);
    puts("New vault: FAT32 mount/file round-trip, automatic FIDO, existing-data guards and failed-setup retry passed");
    return 0;
}
