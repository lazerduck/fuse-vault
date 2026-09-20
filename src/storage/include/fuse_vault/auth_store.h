#ifndef FV_AUTH_STORE_H
#define FV_AUTH_STORE_H
#include "fuse_vault/block_device.h"
#include "fuse_vault/hmac.h"
#include <stdalign.h>
#define FV_TAGS_PER_BLOCK 15u
#define FV_TAG_CACHE_BLOCKS 6u
/* 512-byte metadata sector: 15 state bytes, 17 reserved zero bytes, 15 tags.
 * State 0 = unset, 1 = written; all other values fail integrity checking. */
typedef struct {uint64_t hmac_us,metadata_us,data_us,metadata_reads,metadata_writes;} fv_auth_stats;
typedef struct {
    fv_block_device_t *device;
    uint64_t base,blocks,metadata_blocks,data_base;
    uint64_t bitmap_base,bitmap_blocks,bitmap_cached_sector;
    bool bitmap_cached;
    uint8_t cache_initialized;
    alignas(4) uint8_t bitmap_cache[512];
    uint64_t (*now_us)(void);
    uint8_t volume[16];fv_hmac hmac;
    bool ready;
    uint64_t cache_first;uint32_t cache_count;
    alignas(4) uint8_t cache[FV_TAG_CACHE_BLOCKS*512];
    fv_auth_stats stats;
} fv_auth_store;
/* Open does not format or write. Layout and keys must be trusted by caller. */
fv_block_result_t fv_auth_open(fv_auth_store *,fv_block_device_t *,uint64_t base,
    uint64_t blocks,const uint8_t volume[16],const uint8_t key[32],uint64_t (*now_us)(void));
/* Bitmap precedes metadata at base. Unset bits ignore old metadata/data entirely. */
fv_block_result_t fv_auth_open_bitmap(fv_auth_store *,fv_block_device_t *,uint64_t base,
    uint64_t blocks,const uint8_t volume[16],const uint8_t key[32],uint64_t (*now_us)(void));
void fv_auth_close(fv_auth_store *);
/* Explicit destructive initialization of metadata only. */
fv_block_result_t fv_auth_format(fv_auth_store *);
/* Optional caller-owned aligned scratch (1..256 sectors, must not alias store).
 * Progress counts written bitmap sectors (lazy) or metadata sectors (legacy),
 * not final sync success.
 * Callback must not reenter the store. No change to the disk format. */
typedef void (*fv_format_progress)(void *,uint64_t completed,uint64_t total);
fv_block_result_t fv_auth_format_buffered(fv_auth_store *,uint8_t *scratch,uint32_t sectors,
    fv_format_progress,void *context);
/* Requests are 1..64 whole sectors. Read authenticates the complete requested
 * batch before success; unset_mask indicates zero-filled sectors to NOT decrypt.
 * Buffer must be four-byte aligned. Writes are ciphertext, never plaintext. */
fv_block_result_t fv_auth_read(fv_auth_store *,uint64_t,uint32_t,uint8_t *,uint64_t *unset_mask);
fv_block_result_t fv_auth_write(fv_auth_store *,uint64_t,uint32_t,const uint8_t *);
#endif
