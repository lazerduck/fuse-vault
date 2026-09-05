#include "fuse_vault/journal_authenticator.h"

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

static void test_kmac256_256_bit_output(void) {
    uint8_t key[32];
    for (size_t index = 0u; index < sizeof(key); ++index) {
        key[index] = (uint8_t)(0x40u + index);
    }
    const uint8_t message[] = {0x00u, 0x01u, 0x02u, 0x03u};
    const uint8_t expected[32] = {
        0xb4u,0x23u,0x79u,0x8au,0xc3u,0x8du,0x46u,0x55u,
        0x60u,0xa0u,0x58u,0xb9u,0x82u,0xf5u,0x6fu,0x7fu,
        0xf5u,0xd6u,0x2au,0x5cu,0xfau,0x81u,0x3au,0xb8u,
        0x52u,0x29u,0x98u,0xedu,0x32u,0xe0u,0x0au,0x38u,
    };
    uint8_t output[32];
    CHECK(fv_kmac256(key, sizeof(key), message, sizeof(message), NULL, 0u,
                     output, sizeof(output)));
    CHECK(memcmp(output, expected, sizeof(output)) == 0);
}

static void test_nist_kmac256_sample_four(void) {
    uint8_t key[32];
    for (size_t index = 0u; index < sizeof(key); ++index) key[index] = (uint8_t)(0x40u + index);
    const uint8_t message[] = {0x00u, 0x01u, 0x02u, 0x03u};
    static const uint8_t custom[] = "My Tagged Application";
    const uint8_t expected[64] = {
        0x20u,0xc5u,0x70u,0xc3u,0x13u,0x46u,0xf7u,0x03u,
        0xc9u,0xacu,0x36u,0xc6u,0x1cu,0x03u,0xcbu,0x64u,
        0xc3u,0x97u,0x0du,0x0cu,0xfcu,0x78u,0x7eu,0x9bu,
        0x79u,0x59u,0x9du,0x27u,0x3au,0x68u,0xd2u,0xf7u,
        0xf6u,0x9du,0x4cu,0xc3u,0xdeu,0x9du,0x10u,0x4au,
        0x35u,0x16u,0x89u,0xf2u,0x7cu,0xf6u,0xf5u,0x95u,
        0x1fu,0x01u,0x03u,0xf3u,0x3fu,0x4fu,0x24u,0x87u,
        0x10u,0x24u,0xd9u,0xc2u,0x77u,0x73u,0xa8u,0xddu,
    };
    uint8_t output[64];
    CHECK(fv_kmac256(key, sizeof(key), message, sizeof(message), custom,
                     sizeof(custom) - 1u, output, sizeof(output)));
    CHECK(memcmp(output, expected, sizeof(output)) == 0);
}

static void test_distinct_roots_produce_distinct_tags(void) {
    fv_device_secret_t roots;
    for (size_t index = 0u; index < sizeof(roots.device_secret); ++index) {
        roots.device_secret[index] = (uint8_t)index;
    }
    uint8_t device_id[FV_VAULT_ID_SIZE];
    memset(device_id, 0x5au, sizeof(device_id));
    fv_dual_journal_authenticator_t authenticator;
    CHECK(fv_dual_journal_authenticator_init(&authenticator, &roots,
                                              device_id));
    uint8_t record[FV_JOURNAL_AUTHENTICATED_SIZE] = {0};
    uint8_t tag_a[32];
    uint8_t tag_b[32];
    CHECK(authenticator.interface.ops->compute_tags(
        &authenticator.interface, record, sizeof(record), tag_a, tag_b));
    const uint8_t expected_hmac_key[32] = {
        0x59u,0x68u,0x0eu,0xdeu,0xd5u,0x87u,0x87u,0x22u,
        0x61u,0x90u,0xadu,0x89u,0xa0u,0x4eu,0xf4u,0x39u,
        0x4du,0x89u,0x9cu,0x5eu,0xe3u,0xe8u,0x17u,0x3fu,
        0x18u,0x94u,0x05u,0x16u,0xb9u,0x23u,0x92u,0x3fu,
    };
    const uint8_t expected_tag_a[32] = {
        0xcbu,0x57u,0xaeu,0xe5u,0x75u,0xf0u,0x7du,0x50u,
        0xdbu,0x3fu,0x54u,0xbau,0xb6u,0x3bu,0xcfu,0x63u,
        0x83u,0x90u,0xa5u,0xcau,0x79u,0x7fu,0x8eu,0x9bu,
        0xc7u,0xa3u,0xd9u,0xbeu,0x4cu,0x65u,0x04u,0x41u,
    };
    CHECK(memcmp(authenticator.hmac_key, expected_hmac_key,
                 sizeof(expected_hmac_key)) == 0);
    CHECK(memcmp(tag_a, expected_tag_a, sizeof(expected_tag_a)) == 0);
    CHECK(memcmp(tag_a, tag_b, sizeof(tag_a)) != 0);

    record[0] = 1u;
    uint8_t changed_a[32];
    uint8_t changed_b[32];
    CHECK(authenticator.interface.ops->compute_tags(
        &authenticator.interface, record, sizeof(record), changed_a, changed_b));
    CHECK(memcmp(tag_a, changed_a, sizeof(tag_a)) != 0);
    CHECK(memcmp(tag_b, changed_b, sizeof(tag_b)) != 0);
    fv_dual_journal_authenticator_deinit(&authenticator);
    const uint8_t zero[sizeof(authenticator)] = {0};
    CHECK(memcmp(&authenticator, zero, sizeof(authenticator)) == 0);
}

int main(void) {
    test_kmac256_256_bit_output();
    test_nist_kmac256_sample_four();
    test_distinct_roots_produce_distinct_tags();
    puts("All cryptographic known-answer and separation tests passed.");
    return EXIT_SUCCESS;
}
