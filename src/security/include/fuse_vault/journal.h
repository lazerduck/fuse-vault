#ifndef FV_JOURNAL_H
#define FV_JOURNAL_H
#include "fuse_vault/vault.h"
#define FV_JOURNAL_BYTES 8192u
#define FV_JOURNAL_BANK_BYTES 4096u
#define FV_JOURNAL_RECORD_BYTES 512u
typedef struct {
    void *context;
    int (*read)(void *,uint32_t offset,uint8_t *,size_t);
    int (*program)(void *,uint32_t offset,const uint8_t page[256]);
    int (*erase)(void *,unsigned bank);
} fv_journal_io;
typedef struct {fv_journal_io io;uint8_t key[32],device_id[16];} fv_journal;
enum {FV_JOURNAL_OK=0,FV_JOURNAL_EMPTY=1,FV_JOURNAL_IO=-1,FV_JOURNAL_CORRUPT=-2};
int fv_journal_key(const uint8_t root[32],const uint8_t device_id[16],uint8_t key[32]);
void fv_journal_init(fv_journal *,fv_journal_io,const uint8_t key[32],const uint8_t device_id[16]);
void fv_journal_clear(fv_journal *);
int fv_journal_load(fv_journal *,fv_device_state *);
/* previous=0 allows first seq=1 only; caller must separately prove provisioning
 * was never activated. A blank journal on an enrolled device is NOT fresh state.
 * No retries after uncertain errors. Reload before the next operation. */
int fv_journal_commit(fv_journal *,uint64_t previous,const fv_device_state *);
#endif
