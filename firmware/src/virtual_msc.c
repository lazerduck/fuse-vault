#include "fuse_vault/virtual_msc.h"

#include <stddef.h>

static fv_msc_result_t translate(fv_block_result_t result) {
    switch (result) {
        case FV_BLOCK_OK: return FV_MSC_OK;
        case FV_BLOCK_ERROR_NOT_READY: return FV_MSC_NOT_READY;
        case FV_BLOCK_ERROR_OUT_OF_RANGE: return FV_MSC_OUT_OF_RANGE;
        case FV_BLOCK_ERROR_INTEGRITY: return FV_MSC_INTEGRITY_ERROR;
        case FV_BLOCK_ERROR_INVALID_ARGUMENT: return FV_MSC_INVALID_REQUEST;
        default: return FV_MSC_IO_ERROR;
    }
}

static bool ready(const fv_virtual_msc_t *msc) {
    return msc != NULL && msc->attached && msc->accepting_requests &&
           msc->plaintext_blocks != NULL && msc->plaintext_blocks->ops != NULL &&
           msc->plaintext_blocks->ops->is_present != NULL &&
           msc->plaintext_blocks->ops->is_present(msc->plaintext_blocks);
}

void fv_virtual_msc_init(fv_virtual_msc_t *msc) {
    if (msc != NULL) *msc = (fv_virtual_msc_t){0};
}

bool fv_virtual_msc_attach(fv_virtual_msc_t *msc,
                           fv_block_device_t *plaintext_blocks) {
    if (msc == NULL || plaintext_blocks == NULL || plaintext_blocks->ops == NULL ||
        plaintext_blocks->ops->read == NULL || plaintext_blocks->ops->write == NULL ||
        plaintext_blocks->ops->sync == NULL ||
        plaintext_blocks->ops->block_count == NULL ||
        plaintext_blocks->ops->is_present == NULL ||
        !plaintext_blocks->ops->is_present(plaintext_blocks) ||
        plaintext_blocks->ops->block_count(plaintext_blocks) == 0u) return false;
    msc->plaintext_blocks = plaintext_blocks;
    msc->attached = true;
    msc->accepting_requests = true;
    return true;
}

void fv_virtual_msc_block_requests(fv_virtual_msc_t *msc) {
    if (msc != NULL) msc->accepting_requests = false;
}

void fv_virtual_msc_detach(fv_virtual_msc_t *msc) {
    if (msc != NULL) *msc = (fv_virtual_msc_t){0};
}

fv_msc_result_t fv_virtual_msc_read10(fv_virtual_msc_t *msc,
                                      uint64_t first_block,
                                      uint32_t block_count, uint8_t *output) {
    if (!ready(msc)) return FV_MSC_NOT_READY;
    if (output == NULL || block_count == 0u) return FV_MSC_INVALID_REQUEST;
    for (uint32_t index = 0u; index < block_count; ++index) {
        const fv_msc_result_t result = translate(msc->plaintext_blocks->ops->read(
            msc->plaintext_blocks, first_block + index, 1u,
            output + (size_t)index * FV_BLOCK_SIZE));
        if (result != FV_MSC_OK) return result;
    }
    return FV_MSC_OK;
}

fv_msc_result_t fv_virtual_msc_write10(fv_virtual_msc_t *msc,
                                       uint64_t first_block,
                                       uint32_t block_count,
                                       const uint8_t *input) {
    if (!ready(msc)) return FV_MSC_NOT_READY;
    if (input == NULL || block_count == 0u) return FV_MSC_INVALID_REQUEST;
    for (uint32_t index = 0u; index < block_count; ++index) {
        const fv_msc_result_t result = translate(msc->plaintext_blocks->ops->write(
            msc->plaintext_blocks, first_block + index, 1u,
            input + (size_t)index * FV_BLOCK_SIZE));
        if (result != FV_MSC_OK) return result;
    }
    return FV_MSC_OK;
}

fv_msc_result_t fv_virtual_msc_synchronize_cache(fv_virtual_msc_t *msc) {
    if (!ready(msc)) return FV_MSC_NOT_READY;
    return translate(msc->plaintext_blocks->ops->sync(msc->plaintext_blocks));
}

fv_msc_result_t fv_virtual_msc_start_stop(fv_virtual_msc_t *msc,
                                          bool start, bool load_eject) {
    if (msc == NULL) return FV_MSC_INVALID_REQUEST;
    if (start) return ready(msc) ? FV_MSC_OK : FV_MSC_NOT_READY;
    if (!load_eject) return FV_MSC_OK;
    if (!ready(msc)) return FV_MSC_NOT_READY;
    const fv_msc_result_t result = fv_virtual_msc_synchronize_cache(msc);
    fv_virtual_msc_block_requests(msc);
    return result;
}
