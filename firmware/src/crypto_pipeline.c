#include "fuse_vault/crypto_pipeline.h"

#include "fuse_vault/journal_authenticator.h"

#ifdef PICO_ON_DEVICE
#include "mbedtls/aes.h"
#include "mbedtls/chacha20.h"
#else
#include <openssl/evp.h>
#endif

#include <stddef.h>
#include <string.h>

static const uint8_t key_domain[] = "fuse-vault/v1/data-stack/layer-key";
static const uint8_t iv_domain[] = "fuse-vault/v1/data-stack/layer-iv";

static void clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

static void put16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8u);
}

static void put64(uint8_t *output, uint64_t value) {
    for (unsigned index = 0u; index < 8u; ++index) {
        output[index] = (uint8_t)(value >> (index * 8u));
    }
}

static bool derive_layer_key(const fv_volume_master_key_t *vmk,
                             const uint8_t vault_id[FV_VAULT_ID_SIZE],
                             size_t layer_index,
                             const fv_encryption_layer_descriptor_t *descriptor,
                             uint8_t output[FV_CRYPTO_PIPELINE_KEY_CAPACITY]) {
    uint8_t context[FV_VAULT_ID_SIZE + 8u];
    uint8_t first[32];
    uint8_t second[32];
    memcpy(context, vault_id, FV_VAULT_ID_SIZE);
    put16(context + 16u, (uint16_t)layer_index);
    put16(context + 18u, descriptor->algorithm_id);
    put16(context + 20u, descriptor->algorithm_version);
    put16(context + 22u, 1u);
    bool ok = fv_hmac_sha256(vmk->bytes, sizeof(vmk->bytes), key_domain,
                             sizeof(key_domain) - 1u, context,
                             sizeof(context), first);
    context[22] = 2u;
    if (ok) {
        ok = fv_hmac_sha256(vmk->bytes, sizeof(vmk->bytes), key_domain,
                            sizeof(key_domain) - 1u, context,
                            sizeof(context), second);
    }
    if (ok) {
        memcpy(output, first, sizeof(first));
        memcpy(output + sizeof(first), second, sizeof(second));
    }
    clear(context, sizeof(context));
    clear(first, sizeof(first));
    clear(second, sizeof(second));
    return ok;
}

static bool derive_iv(const fv_crypto_pipeline_t *pipeline, size_t layer_index,
                      uint64_t logical_block, uint64_t generation,
                      const uint8_t epoch[16], uint64_t counter,
                      uint8_t *output, size_t output_length) {
    uint8_t message[FV_VAULT_ID_SIZE + 2u + 2u + 8u + 8u + 16u + 8u];
    size_t position = 0u;
    memcpy(message + position, pipeline->vault_id, FV_VAULT_ID_SIZE);
    position += FV_VAULT_ID_SIZE;
    put16(message + position, (uint16_t)layer_index);
    position += 2u;
    put16(message + position, pipeline->layers[layer_index].algorithm_id);
    position += 2u;
    put64(message + position, logical_block);
    position += 8u;
    put64(message + position, generation);
    position += 8u;
    memcpy(message + position, epoch, 16u);
    position += 16u;
    put64(message + position, counter);
    const bool ok = fv_kmac256(
        pipeline->layers[layer_index].key,
        FV_CRYPTO_PIPELINE_KEY_CAPACITY, message, sizeof(message), iv_domain,
        sizeof(iv_domain) - 1u, output, output_length);
    clear(message, sizeof(message));
    return ok;
}

static bool aes_xts(const uint8_t key[64], const uint8_t tweak[16],
                    bool encrypt, uint8_t block[512]) {
#ifdef PICO_ON_DEVICE
    mbedtls_aes_xts_context context;
    mbedtls_aes_xts_init(&context);
    int result = encrypt
        ? mbedtls_aes_xts_setkey_enc(&context, key, 512u)
        : mbedtls_aes_xts_setkey_dec(&context, key, 512u);
    if (result == 0) {
        result = mbedtls_aes_crypt_xts(
            &context, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
            512u, tweak, block, block);
    }
    mbedtls_aes_xts_free(&context);
    return result == 0;
#else
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    int length = 0;
    int final_length = 0;
    bool ok = context != NULL;
    if (encrypt) {
        ok = ok && EVP_EncryptInit_ex(context, EVP_aes_256_xts(), NULL, key,
                                     tweak) == 1 &&
             EVP_CIPHER_CTX_set_padding(context, 0) == 1 &&
             EVP_EncryptUpdate(context, block, &length, block, 512) == 1 &&
             length == 512 &&
             EVP_EncryptFinal_ex(context, block + length, &final_length) == 1 &&
             final_length == 0;
    } else {
        ok = ok && EVP_DecryptInit_ex(context, EVP_aes_256_xts(), NULL, key,
                                     tweak) == 1 &&
             EVP_CIPHER_CTX_set_padding(context, 0) == 1 &&
             EVP_DecryptUpdate(context, block, &length, block, 512) == 1 &&
             length == 512 &&
             EVP_DecryptFinal_ex(context, block + length, &final_length) == 1 &&
             final_length == 0;
    }
    EVP_CIPHER_CTX_free(context);
    return ok;
#endif
}

static bool chacha20(const uint8_t key[32], const uint8_t nonce[12],
                     uint8_t block[512]) {
#ifdef PICO_ON_DEVICE
    return mbedtls_chacha20_crypt(key, nonce, 0u, sizeof(uint8_t) * 512u,
                                 block, block) == 0;
#else
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    uint8_t iv[16] = {0};
    memcpy(iv + 4u, nonce, 12u);
    int length = 0;
    int final_length = 0;
    bool ok = context != NULL &&
        EVP_EncryptInit_ex(context, EVP_chacha20(), NULL, key, iv) == 1 &&
        EVP_EncryptUpdate(context, block, &length, block, 512) == 1 &&
        length == 512 &&
        EVP_EncryptFinal_ex(context, block + length, &final_length) == 1 &&
        final_length == 0;
    EVP_CIPHER_CTX_free(context);
    clear(iv, sizeof(iv));
    return ok;
#endif
}

bool fv_crypto_pipeline_init(
    fv_crypto_pipeline_t *pipeline,
    const fv_encryption_stack_descriptor_t *descriptor,
    const fv_volume_master_key_t *vmk,
    const uint8_t vault_id[FV_VAULT_ID_SIZE]) {
    if (pipeline == NULL) return false;
    clear(pipeline, sizeof(*pipeline));
    if (vmk == NULL || vault_id == NULL ||
        !fv_crypto_stack_descriptor_valid(descriptor, true)) {
        return false;
    }
    pipeline->descriptor = *descriptor;
    memcpy(pipeline->vault_id, vault_id, FV_VAULT_ID_SIZE);
    for (size_t index = 0u; index < descriptor->layer_count; ++index) {
        pipeline->layers[index].algorithm_id =
            descriptor->layers[index].algorithm_id;
        pipeline->layers[index].algorithm_version =
            descriptor->layers[index].algorithm_version;
        if (!derive_layer_key(vmk, vault_id, index, &descriptor->layers[index],
                              pipeline->layers[index].key)) {
            fv_crypto_pipeline_clear(pipeline);
            return false;
        }
    }
    pipeline->ready = true;
    return true;
}

static bool transform_layer(const fv_crypto_pipeline_t *pipeline, size_t index,
                            uint64_t logical_block, uint64_t generation,
                            const uint8_t epoch[16], uint64_t counter,
                            bool encrypt, uint8_t block[512]) {
    uint8_t iv[16];
    if (!derive_iv(pipeline, index, logical_block, generation, epoch, counter,
                   iv, sizeof(iv))) {
        clear(iv, sizeof(iv));
        return false;
    }
    bool ok = false;
    switch (pipeline->layers[index].algorithm_id) {
        case FV_ENCRYPTION_ALGORITHM_AES_256_XTS:
            ok = aes_xts(pipeline->layers[index].key, iv, encrypt, block);
            break;
        case FV_ENCRYPTION_ALGORITHM_CHACHA20:
            ok = chacha20(pipeline->layers[index].key, iv, block);
            break;
        default:
            ok = false;
            break;
    }
    clear(iv, sizeof(iv));
    return ok;
}

bool fv_crypto_pipeline_encrypt_block(
    const fv_crypto_pipeline_t *pipeline, uint64_t logical_block,
    uint64_t generation, const uint8_t epoch[16], uint64_t counter,
    uint8_t block[512]) {
    if (pipeline == NULL || !pipeline->ready || epoch == NULL || block == NULL) {
        return false;
    }
    for (size_t index = 0u; index < pipeline->descriptor.layer_count; ++index) {
        if (!transform_layer(pipeline, index, logical_block, generation, epoch,
                             counter, true, block)) return false;
    }
    return true;
}

bool fv_crypto_pipeline_decrypt_block(
    const fv_crypto_pipeline_t *pipeline, uint64_t logical_block,
    uint64_t generation, const uint8_t epoch[16], uint64_t counter,
    uint8_t block[512]) {
    if (pipeline == NULL || !pipeline->ready || epoch == NULL || block == NULL) {
        return false;
    }
    for (size_t remaining = pipeline->descriptor.layer_count;
         remaining > 0u; --remaining) {
        if (!transform_layer(pipeline, remaining - 1u, logical_block,
                             generation, epoch, counter, false, block)) {
            clear(block, 512u);
            return false;
        }
    }
    return true;
}

void fv_crypto_pipeline_clear(fv_crypto_pipeline_t *pipeline) {
    if (pipeline != NULL) clear(pipeline, sizeof(*pipeline));
}
