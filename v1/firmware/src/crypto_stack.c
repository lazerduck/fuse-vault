#include "fuse_vault/crypto_stack.h"

#include <string.h>

static const fv_encryption_algorithm_info_t ALGORITHMS[] = {
    /* Ascon is the mandatory authenticated storage envelope in format V1,
     * rather than a selectable same-size transform. Its ID stays reserved. */
    {FV_ENCRYPTION_ALGORITHM_ASCON_AEAD128, 1u, "Ascon-AEAD128", false, false},
    {FV_ENCRYPTION_ALGORITHM_AES_256_XTS, 1u, "AES-256-XTS", true, false},
    {FV_ENCRYPTION_ALGORITHM_CHACHA20, 1u, "ChaCha20", true, false},
    {FV_ENCRYPTION_ALGORITHM_SM4_XTS, 1u, "SM4-XTS", false, false},
};

size_t fv_crypto_stack_algorithm_count(void) {
    return sizeof(ALGORITHMS) / sizeof(ALGORITHMS[0]);
}

const fv_encryption_algorithm_info_t *fv_crypto_stack_algorithm_at(
    size_t index) {
    return index < fv_crypto_stack_algorithm_count() ? &ALGORITHMS[index]
                                                      : NULL;
}

const fv_encryption_algorithm_info_t *fv_crypto_stack_algorithm_find(
    uint16_t algorithm_id, uint16_t algorithm_version) {
    for (size_t index = 0u; index < fv_crypto_stack_algorithm_count(); ++index) {
        if (ALGORITHMS[index].algorithm_id == algorithm_id &&
            ALGORITHMS[index].algorithm_version == algorithm_version) {
            return &ALGORITHMS[index];
        }
    }
    return NULL;
}

bool fv_crypto_stack_descriptor_valid(
    const fv_encryption_stack_descriptor_t *descriptor,
    bool require_available) {
    if (descriptor == NULL ||
        descriptor->format_version != FV_ENCRYPTION_STACK_FORMAT_VERSION ||
        descriptor->reserved != 0u || descriptor->layer_count == 0u ||
        descriptor->layer_count > FV_ENCRYPTION_STACK_MAX_LAYERS) {
        return false;
    }
    for (size_t index = 0u; index < FV_ENCRYPTION_STACK_MAX_LAYERS; ++index) {
        const fv_encryption_layer_descriptor_t *layer =
            &descriptor->layers[index];
        if (index >= descriptor->layer_count) {
            if (layer->algorithm_id != 0u || layer->algorithm_version != 0u) {
                return false;
            }
            continue;
        }
        const fv_encryption_algorithm_info_t *algorithm =
            fv_crypto_stack_algorithm_find(layer->algorithm_id,
                                            layer->algorithm_version);
        if (algorithm == NULL || (require_available && !algorithm->available)) {
            return false;
        }
        if (!algorithm->duplicates_allowed) {
            for (size_t previous = 0u; previous < index; ++previous) {
                if (descriptor->layers[previous].algorithm_id ==
                        layer->algorithm_id &&
                    descriptor->layers[previous].algorithm_version ==
                        layer->algorithm_version) {
                    return false;
                }
            }
        }
    }
    return true;
}

void fv_crypto_stack_default(fv_encryption_stack_descriptor_t *descriptor) {
    if (descriptor == NULL) return;
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->format_version = FV_ENCRYPTION_STACK_FORMAT_VERSION;
    descriptor->layer_count = 1u;
    descriptor->layers[0].algorithm_id =
        FV_ENCRYPTION_ALGORITHM_AES_256_XTS;
    descriptor->layers[0].algorithm_version = 1u;
}

size_t fv_crypto_stack_preset_count(void) { return 4u; }

const char *fv_crypto_stack_preset_name(size_t preset) {
    static const char *const names[] = {
        "AES-XTS", "ChaCha20", "AES then ChaCha", "ChaCha then AES",
    };
    return preset < fv_crypto_stack_preset_count() ? names[preset] : NULL;
}

bool fv_crypto_stack_preset(size_t preset,
                            fv_encryption_stack_descriptor_t *descriptor) {
    if (descriptor == NULL || preset >= fv_crypto_stack_preset_count()) {
        return false;
    }
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->format_version = FV_ENCRYPTION_STACK_FORMAT_VERSION;
    descriptor->layer_count = preset < 2u ? 1u : 2u;
    if (preset == 0u || preset == 2u) {
        descriptor->layers[0] = (fv_encryption_layer_descriptor_t) {
            FV_ENCRYPTION_ALGORITHM_AES_256_XTS, 1u};
    } else {
        descriptor->layers[0] = (fv_encryption_layer_descriptor_t) {
            FV_ENCRYPTION_ALGORITHM_CHACHA20, 1u};
    }
    if (preset == 2u) {
        descriptor->layers[1] = (fv_encryption_layer_descriptor_t) {
            FV_ENCRYPTION_ALGORITHM_CHACHA20, 1u};
    } else if (preset == 3u) {
        descriptor->layers[1] = (fv_encryption_layer_descriptor_t) {
            FV_ENCRYPTION_ALGORITHM_AES_256_XTS, 1u};
    }
    return fv_crypto_stack_descriptor_valid(descriptor, true);
}
