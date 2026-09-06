#include "fuse_vault/media_layout.h"

#include "fuse_vault/journal_authenticator.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define USED_SIZE 112u
#define TAG_OFFSET 224u

static const uint8_t MAGIC[8] = {'F','V','M','E','D','I','A','1'};
static const uint8_t DOMAIN[] = "fuse-vault/v1/media-layout/auth";

static void clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0u;i<8u;i++)p[i]=(uint8_t)(v>>(i*8u));}
static uint16_t get16(const uint8_t*p){return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8u));}
static uint64_t get64(const uint8_t*p){uint64_t v=0u;for(unsigned i=0u;i<8u;i++)v|=(uint64_t)p[i]<<(i*8u);return v;}
static bool equal(const uint8_t*a,const uint8_t*b,size_t n){uint8_t d=0u;for(size_t i=0u;i<n;i++)d|=(uint8_t)(a[i]^b[i]);return d==0u;}
static bool zero(const uint8_t*p,size_t n){uint8_t v=0u;for(size_t i=0u;i<n;i++)v|=p[i];return v==0u;}
static bool blank(const uint8_t*p,size_t n){bool z=true,f=true;for(size_t i=0u;i<n;i++){z=z&&p[i]==0u;f=f&&p[i]==0xffu;}return z||f;}

static bool usable(fv_block_device_t *device) {
    return device != NULL && device->ops != NULL &&
           device->ops->read != NULL && device->ops->write != NULL &&
           device->ops->sync != NULL && device->ops->block_count != NULL &&
           device->ops->is_present != NULL;
}

static bool make_tag(const fv_device_secret_t *roots, const uint8_t *record,
                     uint8_t output[32]) {
    uint8_t key[32];
    bool ok = fv_hmac_sha256(roots->device_secret, FV_DEVICE_SECRET_SIZE,
                             DOMAIN, sizeof(DOMAIN)-1u, NULL, 0u, key);
    if(ok)ok=fv_hmac_sha256(key,sizeof(key),record,TAG_OFFSET,NULL,0u,output);
    clear(key,sizeof(key));return ok;
}

static bool geometry_valid(const fv_media_layout_t *layout) {
    if(layout==NULL||layout->sequence==0u||zero(layout->vault_id,16u)||
       layout->header_start!=FV_MEDIA_HEADER_START||
       layout->header_blocks!=FV_MEDIA_HEADER_BLOCKS||
       layout->data_start!=FV_MEDIA_DATA_START||layout->data_blocks==0u||
       layout->data_blocks%4u!=0u||layout->fido_blocks==0u||
       layout->recovery_blocks<FV_MEDIA_RECOVERY_BLOCKS)return false;
    if(layout->data_start>layout->physical_blocks||
       layout->data_blocks>layout->physical_blocks-layout->data_start)return false;
    if(layout->fido_start!=layout->data_start+layout->data_blocks||
       layout->fido_blocks>layout->physical_blocks-layout->fido_start)return false;
    return layout->recovery_start==layout->fido_start+layout->fido_blocks&&
           layout->recovery_blocks==layout->physical_blocks-layout->recovery_start;
}

static bool same_layout(const fv_media_layout_t *left,
                        const fv_media_layout_t *right) {
    return left->physical_blocks == right->physical_blocks &&
           equal(left->vault_id, right->vault_id, FV_VAULT_ID_SIZE) &&
           left->header_start == right->header_start &&
           left->header_blocks == right->header_blocks &&
           left->data_start == right->data_start &&
           left->data_blocks == right->data_blocks &&
           left->fido_start == right->fido_start &&
           left->fido_blocks == right->fido_blocks &&
           left->recovery_start == right->recovery_start &&
           left->recovery_blocks == right->recovery_blocks;
}

static bool serialize(const fv_media_layout_t *layout,
                      const fv_device_secret_t *roots,uint8_t out[512]) {
    if(!geometry_valid(layout)||roots==NULL)return false;
    memset(out,0,512u);memcpy(out,MAGIC,8u);put16(out+8u,FV_MEDIA_FORMAT_VERSION);
    put16(out+10u,USED_SIZE);put64(out+16u,layout->sequence);
    put64(out+24u,layout->physical_blocks);memcpy(out+32u,layout->vault_id,16u);
    put64(out+48u,layout->header_start);put64(out+56u,layout->header_blocks);
    put64(out+64u,layout->data_start);put64(out+72u,layout->data_blocks);
    put64(out+80u,layout->fido_start);put64(out+88u,layout->fido_blocks);
    put64(out+96u,layout->recovery_start);put64(out+104u,layout->recovery_blocks);
    return make_tag(roots,out,out+TAG_OFFSET);
}

static bool parse(const uint8_t in[512],const fv_device_secret_t *roots,
                  fv_media_layout_t *layout) {
    uint8_t tag[32];bool ok=roots!=NULL&&make_tag(roots,in,tag)&&
        equal(tag,in+TAG_OFFSET,32u)&&memcmp(in,MAGIC,8u)==0&&
        get16(in+8u)==FV_MEDIA_FORMAT_VERSION&&get16(in+10u)==USED_SIZE&&
        get16(in+12u)==0u&&get16(in+14u)==0u&&zero(in+USED_SIZE,TAG_OFFSET-USED_SIZE)&&
        zero(in+TAG_OFFSET+32u,512u-TAG_OFFSET-32u);clear(tag,sizeof(tag));
    if(!ok){clear(layout,sizeof(*layout));return false;}
    *layout=(fv_media_layout_t){.sequence=get64(in+16u),.physical_blocks=get64(in+24u),
        .header_start=get64(in+48u),.header_blocks=get64(in+56u),
        .data_start=get64(in+64u),.data_blocks=get64(in+72u),
        .fido_start=get64(in+80u),.fido_blocks=get64(in+88u),
        .recovery_start=get64(in+96u),.recovery_blocks=get64(in+104u)};
    memcpy(layout->vault_id,in+32u,16u);
    if(!geometry_valid(layout)){clear(layout,sizeof(*layout));return false;}return true;
}

fv_media_result_t fv_media_classify(fv_block_device_t *device) {
    if(!usable(device))return FV_MEDIA_INVALID;
    if(!device->ops->is_present(device))return FV_MEDIA_ABSENT;
    uint8_t blocks[1024];if(device->ops->read(device,0u,2u,blocks)!=FV_BLOCK_OK)
        return FV_MEDIA_IO_ERROR;
    bool first_blank=blank(blocks,512u),second_blank=blank(blocks+512u,512u);
    bool first_magic=memcmp(blocks,MAGIC,8u)==0;
    bool second_magic=memcmp(blocks+512u,MAGIC,8u)==0;
    fv_media_result_t result;
    if(first_blank&&second_blank)result=FV_MEDIA_BLANK;
    else if(first_magic||second_magic){const uint8_t*p=first_magic?blocks:blocks+512u;
        result=get16(p+8u)==FV_MEDIA_FORMAT_VERSION?FV_MEDIA_OK:FV_MEDIA_UNSUPPORTED;}
    else result=FV_MEDIA_FOREIGN;
    clear(blocks,sizeof(blocks));return result;
}

fv_media_result_t fv_media_prepare_for_initialization(
    fv_block_device_t *device) {
    if (!usable(device)) return FV_MEDIA_INVALID;
    if (!device->ops->is_present(device)) return FV_MEDIA_ABSENT;
    uint8_t empty[FV_MEDIA_SUPERBLOCK_SLOTS * FV_BLOCK_SIZE] = {0};
    if (device->ops->write(device, 0u, FV_MEDIA_SUPERBLOCK_SLOTS, empty) !=
            FV_BLOCK_OK ||
        device->ops->sync(device) != FV_BLOCK_OK) {
        clear(empty, sizeof(empty));
        return FV_MEDIA_IO_ERROR;
    }
    clear(empty, sizeof(empty));
    return fv_media_classify(device) == FV_MEDIA_BLANK
        ? FV_MEDIA_OK : FV_MEDIA_IO_ERROR;
}

fv_media_result_t fv_media_load(fv_block_device_t *device,
 const fv_device_secret_t *roots,const uint8_t expected[16],fv_media_layout_t *layout){
    if(layout)clear(layout,sizeof(*layout));
    if(!usable(device)||roots==NULL||layout==NULL)return FV_MEDIA_INVALID;
    if(!device->ops->is_present(device))return FV_MEDIA_ABSENT;
    uint8_t record[512];fv_media_layout_t candidate;bool found=false,invalid=false;
    for(uint64_t slot=0u;slot<2u;slot++){if(device->ops->read(device,slot,1u,record)!=FV_BLOCK_OK){clear(record,sizeof(record));return FV_MEDIA_IO_ERROR;}
        if(parse(record,roots,&candidate)&&candidate.physical_blocks==device->ops->block_count(device)&&(!expected||equal(expected,candidate.vault_id,16u))){
            if(found){uint64_t high=candidate.sequence>layout->sequence?candidate.sequence:layout->sequence;
                uint64_t low=candidate.sequence>layout->sequence?layout->sequence:candidate.sequence;
                if(!same_layout(layout,&candidate)||high-low!=1u){clear(record,sizeof(record));clear(&candidate,sizeof(candidate));clear(layout,sizeof(*layout));return FV_MEDIA_INVALID;}}
            if (!found || candidate.sequence > layout->sequence) {
                *layout = candidate;
            }
            found = true;
        }else if(!blank(record,sizeof(record)))invalid=true;}
    clear(record,sizeof(record));clear(&candidate,sizeof(candidate));return found?FV_MEDIA_OK:(invalid?FV_MEDIA_INVALID:FV_MEDIA_BLANK);
}

fv_media_result_t fv_media_format(fv_block_device_t *device,
 const fv_device_secret_t *roots,const uint8_t vault_id[16],uint64_t fido_blocks,
 fv_media_layout_t *layout){
    if(layout)clear(layout,sizeof(*layout));
    if(!usable(device)||roots==NULL||vault_id==NULL||layout==NULL||zero(vault_id,16u)||fido_blocks==0u)return FV_MEDIA_INVALID;
    if(!device->ops->is_present(device))return FV_MEDIA_ABSENT;
    uint64_t total=device->ops->block_count(device);
    if(total<=FV_MEDIA_DATA_START+fido_blocks+FV_MEDIA_RECOVERY_BLOCKS+4u)return FV_MEDIA_INVALID;
    uint64_t data_blocks=(total-FV_MEDIA_DATA_START-fido_blocks-FV_MEDIA_RECOVERY_BLOCKS)&~UINT64_C(3);
    fv_media_layout_t value={.sequence=1u,.physical_blocks=total,.header_start=FV_MEDIA_HEADER_START,
      .header_blocks=FV_MEDIA_HEADER_BLOCKS,.data_start=FV_MEDIA_DATA_START,.data_blocks=data_blocks,
      .fido_start=FV_MEDIA_DATA_START+data_blocks,.fido_blocks=fido_blocks};
    memcpy(value.vault_id,vault_id,FV_VAULT_ID_SIZE);
    value.recovery_start=value.fido_start+value.fido_blocks;value.recovery_blocks=total-value.recovery_start;
    uint8_t record[512];if(!serialize(&value,roots,record))return FV_MEDIA_INVALID;
    for(uint64_t slot=0u;slot<2u;slot++){value.sequence=slot+1u;if(!serialize(&value,roots,record)||device->ops->write(device,slot,1u,record)!=FV_BLOCK_OK||device->ops->sync(device)!=FV_BLOCK_OK){clear(record,sizeof(record));return FV_MEDIA_IO_ERROR;}}
    clear(record,sizeof(record));return fv_media_load(device,roots,vault_id,layout);
}

static bool open(const fv_media_layout_t*l,fv_block_device_t*d,fv_block_slice_t*s,uint64_t first,uint64_t count){return geometry_valid(l)&&d&&d->ops&&d->ops->block_count&&d->ops->block_count(d)==l->physical_blocks&&fv_block_slice_init(s,d,first,count);}
bool fv_media_open_header(const fv_media_layout_t*l,fv_block_device_t*d,fv_block_slice_t*s){return open(l,d,s,l?l->header_start:0u,l?l->header_blocks:0u);}
bool fv_media_open_data(const fv_media_layout_t*l,fv_block_device_t*d,fv_block_slice_t*s){return open(l,d,s,l?l->data_start:0u,l?l->data_blocks:0u);}
bool fv_media_open_fido(const fv_media_layout_t*l,fv_block_device_t*d,fv_block_slice_t*s){return open(l,d,s,l?l->fido_start:0u,l?l->fido_blocks:0u);}
