#include "fuse_vault/journal_authenticator.h"

#ifdef PICO_ON_DEVICE
#include "pico/sha256.h"
#else
#include <openssl/evp.h>
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define KECCAK_LANES 25u
#define KMAC256_RATE 136u

static const uint64_t ROUND_CONSTANTS[24] = {
    UINT64_C(0x0000000000000001), UINT64_C(0x0000000000008082),
    UINT64_C(0x800000000000808a), UINT64_C(0x8000000080008000),
    UINT64_C(0x000000000000808b), UINT64_C(0x0000000080000001),
    UINT64_C(0x8000000080008081), UINT64_C(0x8000000000008009),
    UINT64_C(0x000000000000008a), UINT64_C(0x0000000000000088),
    UINT64_C(0x0000000080008009), UINT64_C(0x000000008000000a),
    UINT64_C(0x000000008000808b), UINT64_C(0x800000000000008b),
    UINT64_C(0x8000000000008089), UINT64_C(0x8000000000008003),
    UINT64_C(0x8000000000008002), UINT64_C(0x8000000000000080),
    UINT64_C(0x000000000000800a), UINT64_C(0x800000008000000a),
    UINT64_C(0x8000000080008081), UINT64_C(0x8000000000008080),
    UINT64_C(0x0000000080000001), UINT64_C(0x8000000080008008),
};

static const unsigned ROTATION[24] = {
    1u, 3u, 6u, 10u, 15u, 21u, 28u, 36u, 45u, 55u, 2u, 14u,
    27u, 41u, 56u, 8u, 25u, 43u, 62u, 18u, 39u, 61u, 20u, 44u,
};

static const unsigned PILN[24] = {
    10u, 7u, 11u, 17u, 18u, 3u, 5u, 16u, 8u, 21u, 24u, 4u,
    15u, 23u, 19u, 13u, 12u, 2u, 20u, 14u, 22u, 9u, 6u, 1u,
};

typedef struct {
    uint64_t state[KECCAK_LANES];
    size_t position;
} sponge_t;

static uint64_t rotate_left(uint64_t value, unsigned amount) {
    return (value << amount) | (value >> (64u - amount));
}

static void keccak_f1600(uint64_t state[KECCAK_LANES]) {
    for (unsigned round = 0u; round < 24u; ++round) {
        uint64_t bc[5];
        for (unsigned index = 0u; index < 5u; ++index) {
            bc[index] = state[index] ^ state[index + 5u] ^
                        state[index + 10u] ^ state[index + 15u] ^
                        state[index + 20u];
        }
        for (unsigned index = 0u; index < 5u; ++index) {
            const uint64_t t = bc[(index + 4u) % 5u] ^
                               rotate_left(bc[(index + 1u) % 5u], 1u);
            for (unsigned lane = index; lane < KECCAK_LANES; lane += 5u) {
                state[lane] ^= t;
            }
        }
        uint64_t t = state[1];
        for (unsigned index = 0u; index < 24u; ++index) {
            const unsigned lane = PILN[index];
            const uint64_t saved = state[lane];
            state[lane] = rotate_left(t, ROTATION[index]);
            t = saved;
        }
        for (unsigned row = 0u; row < KECCAK_LANES; row += 5u) {
            for (unsigned index = 0u; index < 5u; ++index) bc[index] = state[row + index];
            for (unsigned index = 0u; index < 5u; ++index) {
                state[row + index] ^= (~bc[(index + 1u) % 5u]) &
                                      bc[(index + 2u) % 5u];
            }
        }
        state[0] ^= ROUND_CONSTANTS[round];
    }
}

static void sponge_absorb(sponge_t *sponge, const uint8_t *input, size_t length) {
    uint8_t *bytes = (uint8_t *)sponge->state;
    for (size_t index = 0u; index < length; ++index) {
        bytes[sponge->position++] ^= input[index];
        if (sponge->position == KMAC256_RATE) {
            keccak_f1600(sponge->state);
            sponge->position = 0u;
        }
    }
}

static size_t left_encode(uint64_t value, uint8_t output[9]) {
    size_t bytes = 1u;
    while (bytes < 8u && value >> (bytes * 8u) != 0u) ++bytes;
    output[0] = (uint8_t)bytes;
    for (size_t index = 0u; index < bytes; ++index) {
        output[1u + index] = (uint8_t)(value >> ((bytes - index - 1u) * 8u));
    }
    return bytes + 1u;
}

static size_t right_encode(uint64_t value, uint8_t output[9]) {
    size_t bytes = 1u;
    while (bytes < 8u && value >> (bytes * 8u) != 0u) ++bytes;
    for (size_t index = 0u; index < bytes; ++index) {
        output[index] = (uint8_t)(value >> ((bytes - index - 1u) * 8u));
    }
    output[bytes] = (uint8_t)bytes;
    return bytes + 1u;
}

static void absorb_encoded_string(sponge_t *sponge, const uint8_t *value,
                                  size_t length) {
    uint8_t encoded[9];
    const size_t encoded_length = left_encode((uint64_t)length * 8u, encoded);
    sponge_absorb(sponge, encoded, encoded_length);
    sponge_absorb(sponge, value, length);
}

static void absorb_zero_padding(sponge_t *sponge) {
    static const uint8_t zero = 0u;
    while (sponge->position != 0u) sponge_absorb(sponge, &zero, 1u);
}

static void absorb_bytepad_prefix(sponge_t *sponge) {
    uint8_t encoded[9];
    const size_t length = left_encode(KMAC256_RATE, encoded);
    sponge_absorb(sponge, encoded, length);
}

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)data;
    while (length-- > 0u) *bytes++ = 0u;
}

bool fv_kmac256(const uint8_t *key, size_t key_length,
                const uint8_t *message, size_t message_length,
                const uint8_t *customization, size_t customization_length,
                uint8_t *output, size_t output_length) {
    if (key == NULL || output == NULL ||
        (message == NULL && message_length != 0u) ||
        (customization == NULL && customization_length != 0u) ||
        key_length > SIZE_MAX / 8u || message_length > SIZE_MAX / 8u ||
        customization_length > SIZE_MAX / 8u ||
        output_length > SIZE_MAX / 8u) return false;

    sponge_t sponge = {0};
    static const uint8_t function_name[] = "KMAC";
    absorb_bytepad_prefix(&sponge);
    absorb_encoded_string(&sponge, function_name, sizeof(function_name) - 1u);
    absorb_encoded_string(&sponge, customization, customization_length);
    absorb_zero_padding(&sponge);

    absorb_bytepad_prefix(&sponge);
    absorb_encoded_string(&sponge, key, key_length);
    absorb_zero_padding(&sponge);
    sponge_absorb(&sponge, message, message_length);
    uint8_t encoded[9];
    const size_t encoded_length = right_encode((uint64_t)output_length * 8u,
                                                encoded);
    sponge_absorb(&sponge, encoded, encoded_length);

    uint8_t *bytes = (uint8_t *)sponge.state;
    bytes[sponge.position] ^= 0x04u;
    bytes[KMAC256_RATE - 1u] ^= 0x80u;
    keccak_f1600(sponge.state);
    size_t produced = 0u;
    while (produced < output_length) {
        const size_t remaining = output_length - produced;
        const size_t chunk = remaining < KMAC256_RATE ? remaining : KMAC256_RATE;
        memcpy(output + produced, bytes, chunk);
        produced += chunk;
        if (produced < output_length) keccak_f1600(sponge.state);
    }
    secure_clear(&sponge, sizeof(sponge));
    return true;
}

bool fv_hmac_sha256(const uint8_t *key, size_t key_length,
                    const uint8_t *first, size_t first_length,
                    const uint8_t *second, size_t second_length,
                    uint8_t output[FV_JOURNAL_TAG_SIZE]) {
#ifdef PICO_ON_DEVICE
    if (key_length > 64u) return false;
    uint8_t inner_pad[64];
    uint8_t outer_pad[64];
    memset(inner_pad, 0x36, sizeof(inner_pad));
    memset(outer_pad, 0x5c, sizeof(outer_pad));
    for (size_t index = 0u; index < key_length; ++index) {
        inner_pad[index] ^= key[index];
        outer_pad[index] ^= key[index];
    }
    pico_sha256_state_t state;
    sha256_result_t inner;
    sha256_result_t result;
    if (pico_sha256_try_start(&state, SHA256_BIG_ENDIAN, false) != PICO_OK) {
        secure_clear(inner_pad, sizeof(inner_pad));
        secure_clear(outer_pad, sizeof(outer_pad));
        return false;
    }
    pico_sha256_update_blocking(&state, inner_pad, sizeof(inner_pad));
    pico_sha256_update_blocking(&state, first, first_length);
    if (second_length != 0u) {
        pico_sha256_update_blocking(&state, second, second_length);
    }
    pico_sha256_finish(&state, &inner);
    if (pico_sha256_try_start(&state, SHA256_BIG_ENDIAN, false) != PICO_OK) {
        secure_clear(inner_pad, sizeof(inner_pad));
        secure_clear(outer_pad, sizeof(outer_pad));
        secure_clear(&inner, sizeof(inner));
        return false;
    }
    pico_sha256_update_blocking(&state, outer_pad, sizeof(outer_pad));
    pico_sha256_update_blocking(&state, inner.bytes, sizeof(inner.bytes));
    pico_sha256_finish(&state, &result);
    memcpy(output, result.bytes, FV_JOURNAL_TAG_SIZE);
    secure_clear(inner_pad, sizeof(inner_pad));
    secure_clear(outer_pad, sizeof(outer_pad));
    secure_clear(&inner, sizeof(inner));
    secure_clear(&result, sizeof(result));
    return true;
#else
    uint8_t message[FV_JOURNAL_RECORD_SIZE];
    if (first_length > sizeof(message) ||
        second_length > sizeof(message) - first_length) return false;
    memcpy(message, first, first_length);
    if (second_length != 0u) memcpy(message + first_length, second, second_length);
    size_t output_length = 0u;
    const bool ok = EVP_Q_mac(NULL, "HMAC", NULL, "SHA256", NULL,
                              key, key_length, message,
                              first_length + second_length,
                              output, FV_JOURNAL_TAG_SIZE,
                              &output_length) != NULL &&
                    output_length == FV_JOURNAL_TAG_SIZE;
    secure_clear(message, sizeof(message));
    return ok;
#endif
}

static bool derive_hmac_key(const uint8_t root[FV_DEVICE_ROOT_SIZE],
                            const uint8_t device_id[FV_VAULT_ID_SIZE],
                            uint8_t output[FV_JOURNAL_TAG_SIZE]) {
    static const uint8_t label[] = "fuse-vault/v1/journal/hmac-sha256";
    uint8_t input[4u + sizeof(label) - 1u + 1u + FV_VAULT_ID_SIZE + 4u];
    size_t position = 0u;
    input[position++] = 0u; input[position++] = 0u;
    input[position++] = 0u; input[position++] = 1u;
    memcpy(input + position, label, sizeof(label) - 1u);
    position += sizeof(label) - 1u;
    input[position++] = 0u;
    memcpy(input + position, device_id, FV_VAULT_ID_SIZE);
    position += FV_VAULT_ID_SIZE;
    input[position++] = 0u; input[position++] = 0u;
    input[position++] = 1u; input[position++] = 0u;
    return fv_hmac_sha256(root, FV_DEVICE_ROOT_SIZE, input, position, NULL, 0u,
                       output);
}

static bool compute_tags(fv_journal_authenticator_t *interface,
                         const uint8_t *record, size_t authenticated_length,
                         uint8_t tag_a[FV_JOURNAL_TAG_SIZE],
                         uint8_t tag_b[FV_JOURNAL_TAG_SIZE]) {
    fv_dual_journal_authenticator_t *authenticator = interface->context;
    static const uint8_t hmac_domain[] = "fuse-vault/v1/journal/tag/hmac-sha256";
    static const uint8_t kmac_domain[] = "fuse-vault/v1/journal/tag/kmac256";
    return authenticator != NULL && authenticator->initialized &&
           fv_hmac_sha256(authenticator->hmac_key, sizeof(authenticator->hmac_key),
                       hmac_domain, sizeof(hmac_domain) - 1u,
                       record, authenticated_length, tag_a) &&
           fv_kmac256(authenticator->kmac_key, sizeof(authenticator->kmac_key),
                      record, authenticated_length,
                      kmac_domain, sizeof(kmac_domain) - 1u, tag_b,
                      FV_JOURNAL_TAG_SIZE);
}

static const fv_journal_authenticator_ops_t AUTHENTICATOR_OPS = {
    .compute_tags = compute_tags,
};

bool fv_dual_journal_authenticator_init(
    fv_dual_journal_authenticator_t *authenticator,
    const fv_device_secret_t *device_roots,
    const uint8_t device_id[FV_VAULT_ID_SIZE]) {
    if (authenticator == NULL || device_roots == NULL || device_id == NULL) {
        return false;
    }
    memset(authenticator, 0, sizeof(*authenticator));
    static const uint8_t kmac_label[] =
        "fuse-vault/v1/journal/kmac256";
    const bool ok = derive_hmac_key(device_roots->device_secret, device_id,
                                    authenticator->hmac_key) &&
        fv_kmac256(device_roots->device_secret + FV_DEVICE_ROOT_SIZE,
                   FV_DEVICE_ROOT_SIZE, device_id, FV_VAULT_ID_SIZE,
                   kmac_label, sizeof(kmac_label) - 1u,
                   authenticator->kmac_key, sizeof(authenticator->kmac_key));
    if (!ok) {
        secure_clear(authenticator, sizeof(*authenticator));
        return false;
    }
    authenticator->interface = (fv_journal_authenticator_t) {
        .ops = &AUTHENTICATOR_OPS,
        .context = authenticator,
    };
    authenticator->initialized = true;
    return true;
}

void fv_dual_journal_authenticator_deinit(
    fv_dual_journal_authenticator_t *authenticator) {
    if (authenticator != NULL) secure_clear(authenticator, sizeof(*authenticator));
}
