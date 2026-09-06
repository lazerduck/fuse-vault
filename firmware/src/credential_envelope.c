#include "fuse_vault/credential_envelope.h"

#include "fuse_vault/crypto_stack.h"
#include "fuse_vault/journal_authenticator.h"
#include "crypto_aead.h"

#ifdef PICO_ON_DEVICE
#include "mbedtls/gcm.h"
#else
#include <openssl/evp.h>
#endif

#include <stdint.h>
#include <string.h>

#define AES_KEY_SIZE 32u
#define AES_NONCE_SIZE 12u
#define AES_TAG_SIZE 16u
#define ASCON_KEY_SIZE 16u
#define ASCON_NONCE_SIZE 16u
#define ASCON_TAG_SIZE 16u
#define INNER_ENVELOPE_SIZE (AES_NONCE_SIZE + FV_VMK_SIZE + AES_TAG_SIZE)
#define ENVELOPE_AAD_SIZE 106u

_Static_assert(FV_CREDENTIAL_ENVELOPE_SIZE ==
                   ASCON_NONCE_SIZE + INNER_ENVELOPE_SIZE + ASCON_TAG_SIZE,
               "Credential envelope layout mismatch");
_Static_assert(FV_CREDENTIAL_ENVELOPE_SIZE <= FV_WRAPPED_VMK_CAPACITY,
               "Credential envelope exceeds vault-header capacity");

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

void fv_volume_master_key_clear(fv_volume_master_key_t *vmk) {
    if (vmk != NULL) secure_clear(vmk, sizeof(*vmk));
}

static bool all_zero(const uint8_t *data, size_t length) {
    uint8_t combined = 0u;
    for (size_t index = 0u; index < length; ++index) combined |= data[index];
    return combined == 0u;
}

static void write_u16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8u);
}

static void write_u32(uint8_t *output, uint32_t value) {
    for (unsigned index = 0u; index < 4u; ++index) {
        output[index] = (uint8_t)(value >> (index * 8u));
    }
}

static void write_u64(uint8_t *output, uint64_t value) {
    write_u32(output, (uint32_t)value);
    write_u32(output + 4u, (uint32_t)(value >> 32u));
}

static bool costs_valid(uint32_t branch_a, uint32_t branch_b) {
    return branch_a > 0u && branch_a <= FV_CREDENTIAL_PBKDF2_MAX_ITERATIONS &&
           branch_b > 0u && branch_b <= FV_CREDENTIAL_KMAC_MAX_ITERATIONS;
}

bool fv_vault_header_valid(const fv_vault_header_t *header) {
    return header != NULL && header->sequence != 0u &&
           header->crypto_profile == FV_CRYPTO_PROFILE_DUAL_FAMILY_V1 &&
           fv_secret_method_valid(header->entry_method) &&
           !all_zero(header->vault_id, sizeof(header->vault_id)) &&
           !all_zero(header->branch_a_salt, sizeof(header->branch_a_salt)) &&
           !all_zero(header->branch_b_salt, sizeof(header->branch_b_salt)) &&
           costs_valid(header->branch_a_cost, header->branch_b_cost) &&
           header->wrapped_vmk_length == FV_CREDENTIAL_ENVELOPE_SIZE &&
           fv_crypto_stack_descriptor_valid(&header->encryption_stack, true);
}

static void encode_aad(const fv_vault_header_t *header,
                       uint8_t output[ENVELOPE_AAD_SIZE]) {
    static const uint8_t magic[8] = {'F','V','C','R','E','D','1',0};
    memcpy(output, magic, sizeof(magic));
    write_u64(output + 8u, header->sequence);
    write_u32(output + 16u, (uint32_t)header->crypto_profile);
    write_u32(output + 20u, (uint32_t)header->entry_method);
    memcpy(output + 24u, header->vault_id, FV_VAULT_ID_SIZE);
    memcpy(output + 40u, header->branch_a_salt, FV_SALT_SIZE);
    memcpy(output + 56u, header->branch_b_salt, FV_SALT_SIZE);
    write_u32(output + 72u, header->branch_a_cost);
    write_u32(output + 76u, header->branch_b_cost);
    write_u16(output + 80u, header->wrapped_vmk_length);
    write_u32(output + 82u, 1u); /* envelope format version */
    write_u16(output + 86u, header->encryption_stack.format_version);
    output[88] = header->encryption_stack.layer_count;
    output[89] = header->encryption_stack.reserved;
    for (size_t index = 0u; index < FV_ENCRYPTION_STACK_MAX_LAYERS; ++index) {
        write_u16(output + 90u + index * 4u,
                  header->encryption_stack.layers[index].algorithm_id);
        write_u16(output + 92u + index * 4u,
                  header->encryption_stack.layers[index].algorithm_version);
    }
}

static bool pbkdf2_sha256(const fv_secret_encoding_t *entry,
                          const uint8_t salt[FV_SALT_SIZE],
                          const uint8_t vault_id[FV_VAULT_ID_SIZE],
                          uint32_t iterations, uint8_t output[32]) {
    static const uint8_t domain[] = "fuse-vault/v1/credential/pbkdf2";
    uint8_t salt_material[sizeof(domain) - 1u + FV_SALT_SIZE +
                          FV_VAULT_ID_SIZE + 4u];
    size_t position = 0u;
    memcpy(salt_material + position, domain, sizeof(domain) - 1u);
    position += sizeof(domain) - 1u;
    memcpy(salt_material + position, salt, FV_SALT_SIZE);
    position += FV_SALT_SIZE;
    memcpy(salt_material + position, vault_id, FV_VAULT_ID_SIZE);
    position += FV_VAULT_ID_SIZE;
    salt_material[position++] = 0u;
    salt_material[position++] = 0u;
    salt_material[position++] = 0u;
    salt_material[position++] = 1u;
    uint8_t current[32];
    bool ok = fv_hmac_sha256(entry->bytes, sizeof(entry->bytes), salt_material,
                             position, NULL, 0u, current);
    if (ok) memcpy(output, current, sizeof(current));
    for (uint32_t iteration = 1u; ok && iteration < iterations; ++iteration) {
        ok = fv_hmac_sha256(entry->bytes, sizeof(entry->bytes), current,
                            sizeof(current), NULL, 0u, current);
        for (size_t index = 0u; ok && index < sizeof(current); ++index) {
            output[index] ^= current[index];
        }
    }
    secure_clear(current, sizeof(current));
    secure_clear(salt_material, sizeof(salt_material));
    return ok;
}

static bool iterated_kmac(const fv_secret_encoding_t *entry,
                          const uint8_t salt[FV_SALT_SIZE],
                          const uint8_t vault_id[FV_VAULT_ID_SIZE],
                          uint32_t iterations, uint8_t output[32]) {
    static const uint8_t customization[] =
        "fuse-vault/v1/credential/input-kmac256";
    uint8_t message[FV_SALT_SIZE + FV_VAULT_ID_SIZE + 4u];
    memcpy(message, salt, FV_SALT_SIZE);
    memcpy(message + FV_SALT_SIZE, vault_id, FV_VAULT_ID_SIZE);
    uint8_t current[32];
    memset(current, 0, sizeof(current));
    bool ok = true;
    for (uint32_t iteration = 0u; ok && iteration < iterations; ++iteration) {
        write_u32(message + FV_SALT_SIZE + FV_VAULT_ID_SIZE, iteration + 1u);
        const uint8_t *key = iteration == 0u ? entry->bytes : current;
        const size_t key_length = iteration == 0u ? sizeof(entry->bytes)
                                                   : sizeof(current);
        uint8_t next[32];
        ok = fv_kmac256(key, key_length, message, sizeof(message),
                        customization, sizeof(customization) - 1u,
                        next, sizeof(next));
        if (ok) memcpy(current, next, sizeof(current));
        secure_clear(next, sizeof(next));
    }
    if (ok) memcpy(output, current, sizeof(current));
    secure_clear(current, sizeof(current));
    secure_clear(message, sizeof(message));
    return ok;
}

static bool derive_keys(const fv_secret_encoding_t *entry,
                        const fv_device_secret_t *roots,
                        const fv_vault_header_t *header,
                        uint8_t aes_key[AES_KEY_SIZE],
                        uint8_t ascon_key[ASCON_KEY_SIZE]) {
    uint8_t input_a[32];
    uint8_t input_b[32];
    uint8_t material[32 + FV_VAULT_ID_SIZE];
    static const uint8_t aes_domain[] =
        "fuse-vault/v1/credential/kek/aes256-gcm";
    static const uint8_t ascon_domain[] =
        "fuse-vault/v1/credential/kek/ascon-aead128";
    bool ok = pbkdf2_sha256(entry, header->branch_a_salt, header->vault_id,
                            header->branch_a_cost, input_a) &&
              iterated_kmac(entry, header->branch_b_salt, header->vault_id,
                            header->branch_b_cost, input_b);
    if (ok) {
        memcpy(material, input_a, sizeof(input_a));
        memcpy(material + sizeof(input_a), header->vault_id,
               FV_VAULT_ID_SIZE);
        ok = fv_hmac_sha256(roots->device_secret, FV_DEVICE_ROOT_SIZE,
                            aes_domain, sizeof(aes_domain) - 1u,
                            material, sizeof(material), aes_key);
    }
    if (ok) {
        memcpy(material, input_b, sizeof(input_b));
        memcpy(material + sizeof(input_b), header->vault_id,
               FV_VAULT_ID_SIZE);
        ok = fv_kmac256(roots->device_secret + FV_DEVICE_ROOT_SIZE,
                        FV_DEVICE_ROOT_SIZE, material, sizeof(material),
                        ascon_domain, sizeof(ascon_domain) - 1u,
                        ascon_key, ASCON_KEY_SIZE);
    }
    secure_clear(input_a, sizeof(input_a));
    secure_clear(input_b, sizeof(input_b));
    secure_clear(material, sizeof(material));
    return ok;
}

static bool aes_gcm_encrypt(const uint8_t key[AES_KEY_SIZE],
                            const uint8_t nonce[AES_NONCE_SIZE],
                            const uint8_t *aad, size_t aad_length,
                            const uint8_t plaintext[FV_VMK_SIZE],
                            uint8_t ciphertext[FV_VMK_SIZE],
                            uint8_t tag[AES_TAG_SIZE]) {
#ifdef PICO_ON_DEVICE
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int result = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 256u);
    if (result == 0) {
        result = mbedtls_gcm_crypt_and_tag(
            &context, MBEDTLS_GCM_ENCRYPT, FV_VMK_SIZE, nonce, AES_NONCE_SIZE,
            aad, aad_length, plaintext, ciphertext, AES_TAG_SIZE, tag);
    }
    mbedtls_gcm_free(&context);
    return result == 0;
#else
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    int length = 0;
    int final_length = 0;
    bool ok = context != NULL &&
        EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
                            AES_NONCE_SIZE, NULL) == 1 &&
        EVP_EncryptInit_ex(context, NULL, NULL, key, nonce) == 1 &&
        EVP_EncryptUpdate(context, NULL, &length, aad, (int)aad_length) == 1 &&
        EVP_EncryptUpdate(context, ciphertext, &length, plaintext,
                          FV_VMK_SIZE) == 1 && length == (int)FV_VMK_SIZE &&
        EVP_EncryptFinal_ex(context, ciphertext + length, &final_length) == 1 &&
        final_length == 0 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG,
                            AES_TAG_SIZE, tag) == 1;
    EVP_CIPHER_CTX_free(context);
    return ok;
#endif
}

static bool aes_gcm_decrypt(const uint8_t key[AES_KEY_SIZE],
                            const uint8_t nonce[AES_NONCE_SIZE],
                            const uint8_t *aad, size_t aad_length,
                            const uint8_t ciphertext[FV_VMK_SIZE],
                            const uint8_t tag[AES_TAG_SIZE],
                            uint8_t plaintext[FV_VMK_SIZE]) {
#ifdef PICO_ON_DEVICE
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int result = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 256u);
    if (result == 0) {
        result = mbedtls_gcm_auth_decrypt(
            &context, FV_VMK_SIZE, nonce, AES_NONCE_SIZE, aad, aad_length,
            tag, AES_TAG_SIZE, ciphertext, plaintext);
    }
    mbedtls_gcm_free(&context);
    return result == 0;
#else
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    int length = 0;
    int final_length = 0;
    bool ok = context != NULL &&
        EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
                            AES_NONCE_SIZE, NULL) == 1 &&
        EVP_DecryptInit_ex(context, NULL, NULL, key, nonce) == 1 &&
        EVP_DecryptUpdate(context, NULL, &length, aad, (int)aad_length) == 1 &&
        EVP_DecryptUpdate(context, plaintext, &length, ciphertext,
                          FV_VMK_SIZE) == 1 && length == (int)FV_VMK_SIZE &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG,
                            AES_TAG_SIZE, (void *)(uintptr_t)tag) == 1 &&
        EVP_DecryptFinal_ex(context, plaintext + length, &final_length) == 1 &&
        final_length == 0;
    EVP_CIPHER_CTX_free(context);
    return ok;
#endif
}

fv_credential_result_t fv_credential_envelope_create(
    const fv_secret_encoding_t *entry,
    const fv_device_secret_t *device_roots,
    const fv_credential_costs_t *costs,
    fv_credential_random_fill_fn random_fill, void *random_context,
    fv_vault_header_t *header, fv_volume_master_key_t *vmk) {
    if (entry == NULL || device_roots == NULL || costs == NULL ||
        random_fill == NULL || header == NULL || vmk == NULL ||
        header->sequence == 0u || !fv_secret_method_valid(header->entry_method) ||
        all_zero(header->vault_id, sizeof(header->vault_id)) ||
        !costs_valid(costs->pbkdf2_iterations, costs->kmac_iterations)) {
        return FV_CREDENTIAL_INVALID_ARGUMENT;
    }
    header->crypto_profile = FV_CRYPTO_PROFILE_DUAL_FAMILY_V1;
    header->branch_a_cost = costs->pbkdf2_iterations;
    header->branch_b_cost = costs->kmac_iterations;
    header->wrapped_vmk_length = FV_CREDENTIAL_ENVELOPE_SIZE;
    if (header->encryption_stack.format_version == 0u &&
        header->encryption_stack.layer_count == 0u) {
        fv_crypto_stack_default(&header->encryption_stack);
    }
    if (!fv_crypto_stack_descriptor_valid(&header->encryption_stack, true)) {
        return FV_CREDENTIAL_INVALID_ARGUMENT;
    }
    memset(header->wrapped_vmk, 0, sizeof(header->wrapped_vmk));
    uint8_t inner[INNER_ENVELOPE_SIZE];
    uint8_t aad[ENVELOPE_AAD_SIZE];
    uint8_t aes_key[AES_KEY_SIZE];
    uint8_t ascon_key[ASCON_KEY_SIZE];
    fv_credential_result_t result = FV_CREDENTIAL_RANDOM_FAILED;
    if (!random_fill(random_context, header->branch_a_salt, FV_SALT_SIZE) ||
        !random_fill(random_context, header->branch_b_salt, FV_SALT_SIZE) ||
        !random_fill(random_context, vmk->bytes, sizeof(vmk->bytes)) ||
        !random_fill(random_context, inner, AES_NONCE_SIZE) ||
        !random_fill(random_context, header->wrapped_vmk, ASCON_NONCE_SIZE) ||
        all_zero(header->branch_a_salt, sizeof(header->branch_a_salt)) ||
        all_zero(header->branch_b_salt, sizeof(header->branch_b_salt)) ||
        all_zero(vmk->bytes, sizeof(vmk->bytes))) {
        goto cleanup;
    }
    encode_aad(header, aad);
    if (!derive_keys(entry, device_roots, header, aes_key, ascon_key)) {
        result = FV_CREDENTIAL_DERIVATION_FAILED;
        goto cleanup;
    }
    if (!aes_gcm_encrypt(aes_key, inner, aad, sizeof(aad), vmk->bytes,
                         inner + AES_NONCE_SIZE,
                         inner + AES_NONCE_SIZE + FV_VMK_SIZE)) {
        result = FV_CREDENTIAL_DERIVATION_FAILED;
        goto cleanup;
    }
    unsigned long long outer_length = 0u;
    if (crypto_aead_encrypt(
            header->wrapped_vmk + ASCON_NONCE_SIZE, &outer_length,
            inner, INNER_ENVELOPE_SIZE, aad, sizeof(aad), NULL,
            header->wrapped_vmk, ascon_key) != 0 ||
        outer_length != INNER_ENVELOPE_SIZE + ASCON_TAG_SIZE) {
        result = FV_CREDENTIAL_DERIVATION_FAILED;
        goto cleanup;
    }
    result = FV_CREDENTIAL_OK;

cleanup:
    secure_clear(inner, sizeof(inner));
    secure_clear(aad, sizeof(aad));
    secure_clear(aes_key, sizeof(aes_key));
    secure_clear(ascon_key, sizeof(ascon_key));
    if (result != FV_CREDENTIAL_OK) {
        fv_volume_master_key_clear(vmk);
        memset(header->wrapped_vmk, 0, sizeof(header->wrapped_vmk));
    }
    return result;
}

fv_credential_result_t fv_credential_envelope_open(
    const fv_secret_encoding_t *entry,
    const fv_device_secret_t *device_roots,
    const fv_vault_header_t *header, fv_volume_master_key_t *vmk) {
    if (entry == NULL || device_roots == NULL || !fv_vault_header_valid(header) ||
        vmk == NULL) {
        return FV_CREDENTIAL_INVALID_ARGUMENT;
    }
    fv_volume_master_key_clear(vmk);
    uint8_t inner[INNER_ENVELOPE_SIZE];
    uint8_t aad[ENVELOPE_AAD_SIZE];
    uint8_t aes_key[AES_KEY_SIZE];
    uint8_t ascon_key[ASCON_KEY_SIZE];
    encode_aad(header, aad);
    fv_credential_result_t result = FV_CREDENTIAL_DERIVATION_FAILED;
    if (!derive_keys(entry, device_roots, header, aes_key, ascon_key)) {
        goto cleanup;
    }
    unsigned long long inner_length = 0u;
    if (crypto_aead_decrypt(
            inner, &inner_length, NULL,
            header->wrapped_vmk + ASCON_NONCE_SIZE,
            INNER_ENVELOPE_SIZE + ASCON_TAG_SIZE, aad, sizeof(aad),
            header->wrapped_vmk, ascon_key) != 0 ||
        inner_length != INNER_ENVELOPE_SIZE) {
        result = FV_CREDENTIAL_AUTHENTICATION_FAILED;
        goto cleanup;
    }
    if (!aes_gcm_decrypt(aes_key, inner, aad, sizeof(aad),
                         inner + AES_NONCE_SIZE,
                         inner + AES_NONCE_SIZE + FV_VMK_SIZE,
                         vmk->bytes)) {
        result = FV_CREDENTIAL_AUTHENTICATION_FAILED;
        goto cleanup;
    }
    result = FV_CREDENTIAL_OK;

cleanup:
    secure_clear(inner, sizeof(inner));
    secure_clear(aad, sizeof(aad));
    secure_clear(aes_key, sizeof(aes_key));
    secure_clear(ascon_key, sizeof(ascon_key));
    if (result != FV_CREDENTIAL_OK) fv_volume_master_key_clear(vmk);
    return result;
}
