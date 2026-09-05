#include "fuse_vault/credential_envelope.h"
#include "crypto_aead.h"

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

typedef struct {
    uint8_t next;
} random_fixture_t;

static bool deterministic_random(void *context, uint8_t *output,
                                 size_t length) {
    random_fixture_t *random = context;
    for (size_t index = 0u; index < length; ++index) {
        output[index] = random->next++;
    }
    return true;
}

static bool all_zero(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint8_t combined = 0u;
    for (size_t index = 0u; index < length; ++index) combined |= bytes[index];
    return combined == 0u;
}

static void test_official_ascon_vector(void) {
    uint8_t key[16];
    uint8_t nonce[16];
    for (size_t index = 0u; index < sizeof(key); ++index) {
        key[index] = (uint8_t)index;
        nonce[index] = (uint8_t)(0x10u + index);
    }
    static const uint8_t expected[16] = {
        0x4f, 0x9c, 0x27, 0x82, 0x11, 0xbe, 0xc9, 0x31,
        0x6b, 0xf6, 0x8f, 0x46, 0xee, 0x8b, 0x2e, 0xc6,
    };
    uint8_t ciphertext[16];
    unsigned long long length = 0u;
    CHECK(crypto_aead_encrypt(ciphertext, &length, NULL, 0u, NULL, 0u,
                              NULL, nonce, key) == 0);
    CHECK(length == sizeof(expected));
    CHECK(memcmp(ciphertext, expected, sizeof(expected)) == 0);
}

static void fixture(fv_secret_encoding_t *entry, fv_device_secret_t *roots,
                    fv_vault_header_t *header) {
    for (size_t index = 0u; index < sizeof(entry->bytes); ++index) {
        entry->bytes[index] = (uint8_t)(0x40u + index);
    }
    for (size_t index = 0u; index < sizeof(roots->device_secret); ++index) {
        roots->device_secret[index] = (uint8_t)(index + 1u);
    }
    *header = (fv_vault_header_t) {
        .sequence = 7u,
        .entry_method = 1u,
    };
    for (size_t index = 0u; index < sizeof(header->vault_id); ++index) {
        header->vault_id[index] = (uint8_t)(0xa0u + index);
    }
}

static void expect_authentication_failure(
    const fv_secret_encoding_t *entry, const fv_device_secret_t *roots,
    const fv_vault_header_t *header) {
    fv_volume_master_key_t output;
    memset(&output, 0xa5, sizeof(output));
    CHECK(fv_credential_envelope_open(entry, roots, header, &output) ==
          FV_CREDENTIAL_AUTHENTICATION_FAILED);
    CHECK(all_zero(&output, sizeof(output)));
}

static void test_round_trip_and_tampering(void) {
    fv_secret_encoding_t entry;
    fv_device_secret_t roots;
    fv_vault_header_t header;
    fixture(&entry, &roots, &header);
    const fv_credential_costs_t costs = {
        .pbkdf2_iterations = 3u,
        .kmac_iterations = 3u,
    };
    random_fixture_t random = {.next = 0x20u};
    fv_volume_master_key_t created;
    CHECK(fv_credential_envelope_create(
        &entry, &roots, &costs, deterministic_random, &random,
        &header, &created) == FV_CREDENTIAL_OK);
    CHECK(header.crypto_profile == FV_CRYPTO_PROFILE_DUAL_FAMILY_V1);
    CHECK(header.wrapped_vmk_length == FV_CREDENTIAL_ENVELOPE_SIZE);
    fv_volume_master_key_t opened;
    CHECK(fv_credential_envelope_open(&entry, &roots, &header, &opened) ==
          FV_CREDENTIAL_OK);
    CHECK(memcmp(&created, &opened, sizeof(created)) == 0);

    fv_secret_encoding_t wrong_entry = entry;
    wrong_entry.bytes[7] ^= 1u;
    expect_authentication_failure(&wrong_entry, &roots, &header);
    fv_device_secret_t wrong_roots = roots;
    wrong_roots.device_secret[0] ^= 1u;
    expect_authentication_failure(&entry, &wrong_roots, &header);
    wrong_roots = roots;
    wrong_roots.device_secret[FV_DEVICE_ROOT_SIZE] ^= 1u;
    expect_authentication_failure(&entry, &wrong_roots, &header);

    for (size_t index = 0u; index < FV_CREDENTIAL_ENVELOPE_SIZE; ++index) {
        fv_vault_header_t altered = header;
        altered.wrapped_vmk[index] ^= 1u;
        expect_authentication_failure(&entry, &roots, &altered);
    }
    fv_vault_header_t altered = header;
    altered.sequence ^= 1u;
    expect_authentication_failure(&entry, &roots, &altered);
    altered = header;
    ++altered.entry_method;
    expect_authentication_failure(&entry, &roots, &altered);
    altered = header;
    altered.vault_id[0] ^= 1u;
    expect_authentication_failure(&entry, &roots, &altered);
    altered = header;
    altered.branch_a_salt[0] ^= 1u;
    expect_authentication_failure(&entry, &roots, &altered);
    altered = header;
    altered.branch_b_salt[0] ^= 1u;
    expect_authentication_failure(&entry, &roots, &altered);
    altered = header;
    ++altered.branch_a_cost;
    expect_authentication_failure(&entry, &roots, &altered);
    altered = header;
    ++altered.branch_b_cost;
    expect_authentication_failure(&entry, &roots, &altered);

    fv_volume_master_key_clear(&created);
    fv_volume_master_key_clear(&opened);
}

static void test_rejects_unbounded_costs(void) {
    fv_secret_encoding_t entry;
    fv_device_secret_t roots;
    fv_vault_header_t header;
    fixture(&entry, &roots, &header);
    header.crypto_profile = FV_CRYPTO_PROFILE_DUAL_FAMILY_V1;
    header.branch_a_cost = FV_CREDENTIAL_PBKDF2_MAX_ITERATIONS + 1u;
    header.branch_b_cost = 1u;
    header.wrapped_vmk_length = FV_CREDENTIAL_ENVELOPE_SIZE;
    fv_volume_master_key_t output;
    CHECK(fv_credential_envelope_open(&entry, &roots, &header, &output) ==
          FV_CREDENTIAL_INVALID_ARGUMENT);
}

int main(void) {
    test_official_ascon_vector();
    test_round_trip_and_tampering();
    test_rejects_unbounded_costs();
    puts("All credential-envelope tests passed.");
    return EXIT_SUCCESS;
}
