#include "fuse_vault/crypto_stack.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void test_registry(void) {
    CHECK(fv_crypto_stack_algorithm_count() == 4u);
    const fv_encryption_algorithm_info_t *ascon =
        fv_crypto_stack_algorithm_find(FV_ENCRYPTION_ALGORITHM_ASCON_AEAD128,
                                       1u);
    CHECK(ascon != NULL);
    CHECK(!ascon->available);
    CHECK(fv_crypto_stack_algorithm_find(FV_ENCRYPTION_ALGORITHM_AES_256_XTS,
                                         1u)->available);
    CHECK(fv_crypto_stack_algorithm_find(FV_ENCRYPTION_ALGORITHM_CHACHA20,
                                         1u)->available);
    CHECK(fv_crypto_stack_algorithm_find(99u, 1u) == NULL);
    CHECK(fv_crypto_stack_algorithm_at(fv_crypto_stack_algorithm_count()) ==
          NULL);
}

static void test_descriptor_policy(void) {
    fv_encryption_stack_descriptor_t descriptor;
    fv_crypto_stack_default(&descriptor);
    CHECK(fv_crypto_stack_descriptor_valid(&descriptor, true));

    descriptor.layers[0].algorithm_id = FV_ENCRYPTION_ALGORITHM_SM4_XTS;
    CHECK(fv_crypto_stack_descriptor_valid(&descriptor, false));
    CHECK(!fv_crypto_stack_descriptor_valid(&descriptor, true));

    fv_crypto_stack_default(&descriptor);
    descriptor.layer_count = 2u;
    descriptor.layers[1] = descriptor.layers[0];
    CHECK(!fv_crypto_stack_descriptor_valid(&descriptor, false));

    fv_crypto_stack_default(&descriptor);
    descriptor.layers[1].algorithm_id = 1u;
    CHECK(!fv_crypto_stack_descriptor_valid(&descriptor, true));
    descriptor.layers[1].algorithm_id = 0u;
    descriptor.reserved = 1u;
    CHECK(!fv_crypto_stack_descriptor_valid(&descriptor, true));
}

int main(void) {
    test_registry();
    test_descriptor_policy();
    puts("All crypto-stack registry tests passed.");
    return EXIT_SUCCESS;
}
