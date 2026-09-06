#include "fuse_vault/crypto_pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void fixture(fv_volume_master_key_t *vmk, uint8_t vault_id[16],
                    uint8_t block[512]) {
    for (size_t index = 0u; index < sizeof(vmk->bytes); ++index) {
        vmk->bytes[index] = (uint8_t)index;
    }
    for (size_t index = 0u; index < 16u; ++index) {
        vault_id[index] = (uint8_t)(0xa0u + index);
    }
    for (size_t index = 0u; index < 512u; ++index) {
        block[index] = (uint8_t)(index ^ (index >> 3u));
    }
}

static fv_encryption_stack_descriptor_t two_layers(bool reverse) {
    fv_encryption_stack_descriptor_t descriptor = {
        .format_version = FV_ENCRYPTION_STACK_FORMAT_VERSION,
        .layer_count = 2u,
    };
    descriptor.layers[reverse ? 1u : 0u] =
        (fv_encryption_layer_descriptor_t) {
            FV_ENCRYPTION_ALGORITHM_AES_256_XTS, 1u};
    descriptor.layers[reverse ? 0u : 1u] =
        (fv_encryption_layer_descriptor_t) {
            FV_ENCRYPTION_ALGORITHM_CHACHA20, 1u};
    return descriptor;
}

static void test_round_trip_and_order(void) {
    fv_volume_master_key_t vmk;
    uint8_t vault_id[16];
    uint8_t plain[512];
    fixture(&vmk, vault_id, plain);
    uint8_t epoch[16];
    memset(epoch, 0x3c, sizeof(epoch));

    const fv_encryption_stack_descriptor_t forward = two_layers(false);
    const fv_encryption_stack_descriptor_t reverse = two_layers(true);
    fv_crypto_pipeline_t first;
    fv_crypto_pipeline_t second;
    CHECK(fv_crypto_pipeline_init(&first, &forward, &vmk, vault_id));
    CHECK(fv_crypto_pipeline_init(&second, &reverse, &vmk, vault_id));

    uint8_t first_ciphertext[512];
    uint8_t second_ciphertext[512];
    memcpy(first_ciphertext, plain, sizeof(plain));
    memcpy(second_ciphertext, plain, sizeof(plain));
    CHECK(fv_crypto_pipeline_encrypt_block(&first, 7u, 2u, epoch, 9u,
                                           first_ciphertext));
    CHECK(fv_crypto_pipeline_encrypt_block(&second, 7u, 2u, epoch, 9u,
                                           second_ciphertext));
    CHECK(memcmp(first_ciphertext, plain, sizeof(plain)) != 0);
    CHECK(memcmp(second_ciphertext, plain, sizeof(plain)) != 0);
    CHECK(memcmp(first_ciphertext, second_ciphertext, sizeof(plain)) != 0);
    CHECK(fv_crypto_pipeline_decrypt_block(&first, 7u, 2u, epoch, 9u,
                                           first_ciphertext));
    CHECK(fv_crypto_pipeline_decrypt_block(&second, 7u, 2u, epoch, 9u,
                                           second_ciphertext));
    CHECK(memcmp(first_ciphertext, plain, sizeof(plain)) == 0);
    CHECK(memcmp(second_ciphertext, plain, sizeof(plain)) == 0);

    fv_crypto_pipeline_clear(&first);
    fv_crypto_pipeline_clear(&second);
    uint8_t zero[sizeof(first)];
    memset(zero, 0, sizeof(zero));
    CHECK(memcmp(&first, zero, sizeof(first)) == 0);
    fv_volume_master_key_clear(&vmk);
}

static void test_unavailable_fails(void) {
    fv_volume_master_key_t vmk;
    uint8_t vault_id[16];
    uint8_t block[512];
    fixture(&vmk, vault_id, block);
    fv_encryption_stack_descriptor_t descriptor;
    fv_crypto_stack_default(&descriptor);
    descriptor.layers[0].algorithm_id = FV_ENCRYPTION_ALGORITHM_SM4_XTS;
    fv_crypto_pipeline_t pipeline;
    CHECK(!fv_crypto_pipeline_init(&pipeline, &descriptor, &vmk, vault_id));
    CHECK(!pipeline.ready);
}

int main(void) {
    test_round_trip_and_order();
    test_unavailable_fails();
    puts("All crypto-pipeline tests passed.");
    return EXIT_SUCCESS;
}
