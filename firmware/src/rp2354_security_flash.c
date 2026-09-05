#include "fuse_vault/rp2354_security_flash.h"

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/platform.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef FUSE_VAULT_SECURITY_JOURNAL_OFFSET_BYTES
#error "Board definition must reserve the security journal flash offset"
#endif

#ifndef FUSE_VAULT_SECURITY_JOURNAL_SIZE_BYTES
#error "Board definition must reserve the security journal flash size"
#endif

#define FLASH_COORDINATION_TIMEOUT_MS 1000u

_Static_assert(FUSE_VAULT_SECURITY_JOURNAL_OFFSET_BYTES +
                   FUSE_VAULT_SECURITY_JOURNAL_SIZE_BYTES ==
               PICO_FLASH_SIZE_BYTES,
               "Security journal must occupy the end of stacked flash");
_Static_assert(FUSE_VAULT_SECURITY_JOURNAL_SIZE_BYTES ==
                   FV_JOURNAL_MINIMUM_ERASE_BLOCKS * FLASH_SECTOR_SIZE,
               "Security journal geometry must match the portable core");

extern uint8_t __flash_binary_end;

typedef enum {
    FV_FLASH_PROGRAM,
    FV_FLASH_ERASE,
} flash_operation_t;

typedef struct {
    flash_operation_t operation;
    uint32_t absolute_offset;
    const uint8_t *input;
    size_t length;
} flash_request_t;

static bool range_is_valid(size_t offset, size_t length) {
    return offset <= FUSE_VAULT_SECURITY_JOURNAL_SIZE_BYTES &&
           length <= FUSE_VAULT_SECURITY_JOURNAL_SIZE_BYTES - offset;
}

static void __not_in_flash_func(execute_flash_request)(void *context) {
    flash_request_t *request = (flash_request_t *)context;
    if (request->operation == FV_FLASH_PROGRAM) {
        flash_range_program(request->absolute_offset, request->input,
                            request->length);
    } else {
        flash_range_erase(request->absolute_offset, request->length);
    }
}

static bool read_flash(fv_journal_flash_t *flash, size_t offset,
                       uint8_t *output, size_t length) {
    (void)flash;
    if (output == NULL || !range_is_valid(offset, length)) return false;
    const uintptr_t address = (uintptr_t)XIP_BASE +
        FUSE_VAULT_SECURITY_JOURNAL_OFFSET_BYTES + offset;
    memcpy(output, (const void *)address, length);
    return true;
}

static bool run_request(flash_request_t *request) {
    return flash_safe_execute(execute_flash_request, request,
                              FLASH_COORDINATION_TIMEOUT_MS) == PICO_OK;
}

static bool program_flash(fv_journal_flash_t *flash, size_t offset,
                          const uint8_t *input, size_t length) {
    (void)flash;
    if (input == NULL || length == 0u || !range_is_valid(offset, length) ||
        offset % FLASH_PAGE_SIZE != 0u || length % FLASH_PAGE_SIZE != 0u) {
        return false;
    }
    flash_request_t request = {
        .operation = FV_FLASH_PROGRAM,
        .absolute_offset = (uint32_t)
            (FUSE_VAULT_SECURITY_JOURNAL_OFFSET_BYTES + offset),
        .input = input,
        .length = length,
    };
    if (!run_request(&request)) return false;
    const uintptr_t address = (uintptr_t)XIP_BASE + request.absolute_offset;
    return memcmp((const void *)address, input, length) == 0;
}

static bool erase_flash(fv_journal_flash_t *flash, size_t offset,
                        size_t length) {
    (void)flash;
    if (length == 0u || !range_is_valid(offset, length) ||
        offset % FLASH_SECTOR_SIZE != 0u ||
        length % FLASH_SECTOR_SIZE != 0u) return false;
    flash_request_t request = {
        .operation = FV_FLASH_ERASE,
        .absolute_offset = (uint32_t)
            (FUSE_VAULT_SECURITY_JOURNAL_OFFSET_BYTES + offset),
        .input = NULL,
        .length = length,
    };
    if (!run_request(&request)) return false;
    const uint8_t *contents = (const uint8_t *)
        ((uintptr_t)XIP_BASE + request.absolute_offset);
    for (size_t index = 0u; index < length; ++index) {
        if (contents[index] != 0xffu) return false;
    }
    return true;
}

static const fv_journal_flash_ops_t FLASH_OPS = {
    .read = read_flash,
    .program = program_flash,
    .erase = erase_flash,
};

bool fv_rp2354_security_flash_init(fv_journal_flash_t *flash) {
    if (flash == NULL ||
        FUSE_VAULT_SECURITY_JOURNAL_SIZE_BYTES != 2u * FLASH_SECTOR_SIZE ||
        FUSE_VAULT_SECURITY_JOURNAL_OFFSET_BYTES % FLASH_SECTOR_SIZE != 0u) {
        return false;
    }
    const uintptr_t binary_end_offset =
        (uintptr_t)&__flash_binary_end - (uintptr_t)XIP_BASE;
    if (binary_end_offset > FUSE_VAULT_SECURITY_JOURNAL_OFFSET_BYTES) {
        return false;
    }
    *flash = (fv_journal_flash_t) {
        .ops = &FLASH_OPS,
        .context = NULL,
        .size = FUSE_VAULT_SECURITY_JOURNAL_SIZE_BYTES,
        .erase_block_size = FLASH_SECTOR_SIZE,
        .program_size = FLASH_PAGE_SIZE,
    };
    return true;
}
