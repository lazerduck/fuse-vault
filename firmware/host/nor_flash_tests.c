#include "nor_flash.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

static void check(bool condition, const char *expression, const char *file,
                  int line) {
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    uint8_t storage[512];
    fv_nor_flash_t flash;
    CHECK(fv_nor_init(&flash, storage, sizeof(storage), 256u, 4u));

    const uint8_t first[4] = {0xf0u, 0x0fu, 0xaau, 0x55u};
    CHECK(fv_nor_program(&flash, 0u, first, sizeof(first)) == FV_NOR_OK);
    CHECK(memcmp(storage, first, sizeof(first)) == 0);

    const uint8_t illegal[4] = {0xffu, 0x0fu, 0xaau, 0x55u};
    CHECK(fv_nor_program(&flash, 0u, illegal, sizeof(illegal)) ==
          FV_NOR_NEEDS_ERASE);
    CHECK(fv_nor_erase(&flash, 0u, 256u) == FV_NOR_OK);
    CHECK(storage[0] == 0xffu);

    const uint8_t record[8] = {0,1,2,3,4,5,6,7};
    fv_nor_fail_after(&flash, 3u);
    CHECK(fv_nor_program(&flash, 0u, record, sizeof(record)) ==
          FV_NOR_POWER_LOSS);
    CHECK(storage[0] == 0u);
    CHECK(storage[1] == 1u);
    CHECK(storage[2] == 2u);
    CHECK(storage[3] == 0xffu);

    fv_nor_disable_failure(&flash);
    CHECK(fv_nor_program(&flash, 4u, record + 4u, 4u) == FV_NOR_OK);
    CHECK(fv_nor_erase(&flash, 1u, 256u) == FV_NOR_INVALID_ARGUMENT);
    puts("All limited NOR semantics tests passed.");
    return EXIT_SUCCESS;
}
