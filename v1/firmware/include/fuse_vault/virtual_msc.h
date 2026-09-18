#ifndef FUSE_VAULT_VIRTUAL_MSC_H
#define FUSE_VAULT_VIRTUAL_MSC_H

#include "fuse_vault/block_device.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FV_MSC_OK = 0,
    FV_MSC_NOT_READY,
    FV_MSC_INVALID_REQUEST,
    FV_MSC_OUT_OF_RANGE,
    FV_MSC_IO_ERROR,
    FV_MSC_INTEGRITY_ERROR,
} fv_msc_result_t;

/* Host-side SCSI/MSC boundary.  This is intentionally independent of TinyUSB:
 * production callbacks can translate their requests into this small API. */
typedef struct {
    fv_block_device_t *plaintext_blocks;
    bool attached;
    bool accepting_requests;
} fv_virtual_msc_t;

void fv_virtual_msc_init(fv_virtual_msc_t *msc);
bool fv_virtual_msc_attach(fv_virtual_msc_t *msc,
                           fv_block_device_t *plaintext_blocks);
void fv_virtual_msc_block_requests(fv_virtual_msc_t *msc);
void fv_virtual_msc_detach(fv_virtual_msc_t *msc);
fv_msc_result_t fv_virtual_msc_read10(fv_virtual_msc_t *msc,
                                      uint64_t first_block,
                                      uint32_t block_count, uint8_t *output);
fv_msc_result_t fv_virtual_msc_write10(fv_virtual_msc_t *msc,
                                       uint64_t first_block,
                                       uint32_t block_count,
                                       const uint8_t *input);
fv_msc_result_t fv_virtual_msc_synchronize_cache(fv_virtual_msc_t *msc);
fv_msc_result_t fv_virtual_msc_start_stop(fv_virtual_msc_t *msc,
                                          bool start, bool load_eject);

#endif
