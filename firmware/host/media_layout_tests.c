#include "fuse_vault/media_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCKS 256u
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

typedef struct {
    uint8_t bytes[BLOCKS * FV_BLOCK_SIZE];
    bool present;
    bool fail_sync;
} memory_t;

static bool range(uint64_t first, uint32_t count) {
    return count > 0u && first < BLOCKS && (uint64_t)count <= BLOCKS - first;
}
static fv_block_result_t read_blocks(fv_block_device_t *device,uint64_t first,
 uint32_t count,uint8_t *output){memory_t*m=device->context;if(!m->present)return FV_BLOCK_ERROR_NOT_READY;if(!output||!range(first,count))return FV_BLOCK_ERROR_OUT_OF_RANGE;memcpy(output,m->bytes+(size_t)first*512u,(size_t)count*512u);return FV_BLOCK_OK;}
static fv_block_result_t write_blocks(fv_block_device_t *device,uint64_t first,
 uint32_t count,const uint8_t *input){memory_t*m=device->context;if(!m->present)return FV_BLOCK_ERROR_NOT_READY;if(!input||!range(first,count))return FV_BLOCK_ERROR_OUT_OF_RANGE;memcpy(m->bytes+(size_t)first*512u,input,(size_t)count*512u);return FV_BLOCK_OK;}
static fv_block_result_t sync_blocks(fv_block_device_t*d){memory_t*m=d->context;return m->fail_sync?FV_BLOCK_ERROR_IO:FV_BLOCK_OK;}
static uint64_t block_count(const fv_block_device_t*d){(void)d;return BLOCKS;}
static bool is_present(const fv_block_device_t*d){const memory_t*m=d->context;return m->present;}
static const fv_block_device_ops_t OPS={read_blocks,write_blocks,sync_blocks,block_count,is_present};

static fv_device_secret_t roots(uint8_t start){fv_device_secret_t value;for(size_t i=0u;i<sizeof(value.device_secret);i++)value.device_secret[i]=(uint8_t)(start+i);return value;}

static void test_lifecycle(void) {
    memory_t memory = {.present=true};
    fv_block_device_t device = {.ops=&OPS,.context=&memory};
    CHECK(fv_media_classify(&device)==FV_MEDIA_BLANK);
    memory.bytes[0]=0x33u;
    CHECK(fv_media_classify(&device)==FV_MEDIA_FOREIGN);
    CHECK(fv_media_prepare_for_initialization(&device)==FV_MEDIA_OK);
    CHECK(fv_media_classify(&device)==FV_MEDIA_BLANK);

    fv_device_secret_t secret=roots(1u);
    uint8_t vault_id[16];for(size_t i=0u;i<16u;i++)vault_id[i]=(uint8_t)(0xa0u+i);
    fv_media_layout_t layout;
    CHECK(fv_media_format(&device,&secret,vault_id,16u,&layout)==FV_MEDIA_OK);
    CHECK(fv_media_classify(&device)==FV_MEDIA_OK);
    CHECK(layout.sequence==2u);
    CHECK(layout.header_start==2u&&layout.header_blocks==2u);
    CHECK(layout.data_start==32u&&layout.data_blocks%4u==0u);
    CHECK(layout.fido_blocks==16u&&layout.recovery_blocks>=32u);

    fv_media_layout_t loaded;
    CHECK(fv_media_load(&device,&secret,vault_id,&loaded)==FV_MEDIA_OK);
    CHECK(memcmp(&layout,&loaded,sizeof(layout))==0);
    uint8_t second_superblock[FV_BLOCK_SIZE];
    memcpy(second_superblock,memory.bytes+FV_BLOCK_SIZE,FV_BLOCK_SIZE);
    memcpy(memory.bytes+FV_BLOCK_SIZE,memory.bytes,FV_BLOCK_SIZE);
    CHECK(fv_media_load(&device,&secret,vault_id,&loaded)==FV_MEDIA_INVALID);
    memcpy(memory.bytes+FV_BLOCK_SIZE,second_superblock,FV_BLOCK_SIZE);
    CHECK(fv_media_load(&device,&secret,vault_id,&loaded)==FV_MEDIA_OK);
    fv_block_slice_t header,data,fido;
    CHECK(fv_media_open_header(&loaded,&device,&header));
    CHECK(fv_media_open_data(&loaded,&device,&data));
    CHECK(fv_media_open_fido(&loaded,&device,&fido));
    CHECK(header.interface.ops->block_count(&header.interface)==2u);
    CHECK(data.interface.ops->block_count(&data.interface)==layout.data_blocks);
    CHECK(fido.interface.ops->block_count(&fido.interface)==16u);

    fv_device_secret_t wrong=roots(2u);
    CHECK(fv_media_load(&device,&wrong,NULL,&loaded)==FV_MEDIA_INVALID);
    memory.bytes[512u+80u]^=1u;
    CHECK(fv_media_load(&device,&secret,vault_id,&loaded)==FV_MEDIA_OK);
    CHECK(loaded.sequence==1u);
    memory.bytes[80u]^=1u;
    CHECK(fv_media_load(&device,&secret,vault_id,&loaded)==FV_MEDIA_INVALID);

    memory.present=false;
    CHECK(fv_media_classify(&device)==FV_MEDIA_ABSENT);
}

int main(void) {
    test_lifecycle();
    puts("All media-layout tests passed.");
    return EXIT_SUCCESS;
}
