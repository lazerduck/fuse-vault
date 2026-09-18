#include "nor_flash.h"

#include <stdint.h>
#include <string.h>

static bool range_is_valid(const fv_nor_flash_t *flash, size_t offset,
                           size_t length) {
    return flash != NULL && offset <= flash->size && length <= flash->size - offset;
}

bool fv_nor_init(fv_nor_flash_t *flash, uint8_t *storage, size_t size,
                 size_t erase_block_size, size_t program_unit_size) {
    if (flash == NULL || storage == NULL || size == 0u ||
        erase_block_size == 0u || program_unit_size == 0u ||
        size % erase_block_size != 0u ||
        erase_block_size % program_unit_size != 0u) return false;
    *flash = (fv_nor_flash_t) {
        .bytes = storage,
        .size = size,
        .erase_block_size = erase_block_size,
        .program_unit_size = program_unit_size,
        .fail_after_program_bytes = SIZE_MAX,
        .programmed_bytes = 0u,
    };
    memset(storage, 0xff, size);
    return true;
}

fv_nor_result_t fv_nor_read(const fv_nor_flash_t *flash, size_t offset,
                            uint8_t *output, size_t length) {
    if (!range_is_valid(flash, offset, length) ||
        (output == NULL && length != 0u)) return FV_NOR_INVALID_ARGUMENT;
    memcpy(output, flash->bytes + offset, length);
    return FV_NOR_OK;
}

fv_nor_result_t fv_nor_program(fv_nor_flash_t *flash, size_t offset,
                               const uint8_t *input, size_t length) {
    if (!range_is_valid(flash, offset, length) ||
        (input == NULL && length != 0u) ||
        offset % flash->program_unit_size != 0u ||
        length % flash->program_unit_size != 0u) {
        return FV_NOR_INVALID_ARGUMENT;
    }
    for (size_t index = 0u; index < length; ++index) {
        if ((flash->bytes[offset + index] & input[index]) != input[index]) {
            return FV_NOR_NEEDS_ERASE;
        }
    }
    for (size_t index = 0u; index < length; ++index) {
        if (flash->programmed_bytes >= flash->fail_after_program_bytes) {
            return FV_NOR_POWER_LOSS;
        }
        flash->bytes[offset + index] &= input[index];
        ++flash->programmed_bytes;
    }
    return FV_NOR_OK;
}

fv_nor_result_t fv_nor_erase(fv_nor_flash_t *flash, size_t offset,
                             size_t length) {
    if (!range_is_valid(flash, offset, length) ||
        offset % flash->erase_block_size != 0u ||
        length % flash->erase_block_size != 0u) {
        return FV_NOR_INVALID_ARGUMENT;
    }
    memset(flash->bytes + offset, 0xff, length);
    return FV_NOR_OK;
}

void fv_nor_fail_after(fv_nor_flash_t *flash, size_t programmed_bytes) {
    if (flash == NULL) return;
    flash->programmed_bytes = 0u;
    flash->fail_after_program_bytes = programmed_bytes;
}

void fv_nor_disable_failure(fv_nor_flash_t *flash) {
    if (flash == NULL) return;
    flash->programmed_bytes = 0u;
    flash->fail_after_program_bytes = SIZE_MAX;
}
