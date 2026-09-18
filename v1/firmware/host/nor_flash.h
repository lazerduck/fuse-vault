#ifndef FUSE_VAULT_NOR_FLASH_H
#define FUSE_VAULT_NOR_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    FV_NOR_OK = 0,
    FV_NOR_INVALID_ARGUMENT,
    FV_NOR_NEEDS_ERASE,
    FV_NOR_POWER_LOSS,
} fv_nor_result_t;

typedef struct {
    uint8_t *bytes;
    size_t size;
    size_t erase_block_size;
    size_t program_unit_size;
    size_t fail_after_program_bytes;
    size_t programmed_bytes;
} fv_nor_flash_t;

bool fv_nor_init(fv_nor_flash_t *flash, uint8_t *storage, size_t size,
                 size_t erase_block_size, size_t program_unit_size);
fv_nor_result_t fv_nor_read(const fv_nor_flash_t *flash, size_t offset,
                            uint8_t *output, size_t length);
fv_nor_result_t fv_nor_program(fv_nor_flash_t *flash, size_t offset,
                               const uint8_t *input, size_t length);
fv_nor_result_t fv_nor_erase(fv_nor_flash_t *flash, size_t offset,
                             size_t length);
void fv_nor_fail_after(fv_nor_flash_t *flash, size_t programmed_bytes);
void fv_nor_disable_failure(fv_nor_flash_t *flash);

#endif
