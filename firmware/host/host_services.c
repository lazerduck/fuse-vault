#define _GNU_SOURCE

#include "host_services.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define RECORD_SIZE 256u
#define RECORD_HEADER_SIZE 24u
#define RECORD_PAYLOAD_CAPACITY (RECORD_SIZE - RECORD_HEADER_SIZE - 4u)
#define RECORD_FORMAT_VERSION 1u
#define SECRET_PAYLOAD_SIZE FV_DEVICE_SECRET_SIZE
#define SECURITY_PAYLOAD_SIZE 8u
#define VAULT_PAYLOAD_SIZE 194u

static const uint8_t SECRET_MAGIC[8] = {'F','V','S','E','C','D','1',0};
static const uint8_t ACTIVE_MAGIC[8] = {'F','V','A','C','T','D','1',0};
static const uint8_t REVOKE_MAGIC[8] = {'F','V','R','E','V','D','1',0};
static const uint8_t SECURITY_MAGIC[8] = {'F','V','S','T','A','D','1',0};
static const uint8_t VAULT_MAGIC[8] = {'F','V','H','D','R','D','1',0};

static uint32_t read_u32(const uint8_t *input) {
    return (uint32_t)input[0] |
           ((uint32_t)input[1] << 8u) |
           ((uint32_t)input[2] << 16u) |
           ((uint32_t)input[3] << 24u);
}

static uint64_t read_u64(const uint8_t *input) {
    return (uint64_t)read_u32(input) |
           ((uint64_t)read_u32(input + 4u) << 32u);
}

static void write_u16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8u);
}

static uint16_t read_u16(const uint8_t *input) {
    return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8u));
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

static uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t value = 0xffffffffu;
    for (size_t index = 0u; index < length; ++index) {
        value ^= data[index];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(value & 1u);
            value = (value >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~value;
}

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

static bool make_path(const fv_host_services_context_t *context,
                      const char *name, unsigned slot, char *output,
                      size_t capacity) {
    const int result = snprintf(output, capacity, "%s/%s.%u",
                                context->directory, name, slot);
    return result >= 0 && (size_t)result < capacity;
}

static bool write_all(int descriptor, const uint8_t *data, size_t length) {
    size_t written = 0u;
    while (written < length) {
        const ssize_t result = write(descriptor, data + written,
                                     length - written);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        written += (size_t)result;
    }
    return true;
}

static bool read_all(int descriptor, uint8_t *data, size_t length) {
    size_t consumed = 0u;
    while (consumed < length) {
        const ssize_t result = read(descriptor, data + consumed,
                                    length - consumed);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        consumed += (size_t)result;
    }
    return true;
}

static fv_persist_result_t read_record(const char *path, const uint8_t magic[8],
                                       uint8_t *payload, uint32_t expected_length,
                                       uint64_t *sequence) {
    uint8_t record[RECORD_SIZE];
    const int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return errno == ENOENT ? FV_PERSIST_NOT_FOUND : FV_PERSIST_IO_ERROR;
    }
    const bool read_ok = read_all(descriptor, record, sizeof(record));
    const int close_result = close(descriptor);
    if (close_result != 0) return FV_PERSIST_IO_ERROR;
    if (!read_ok) return FV_PERSIST_INVALID;

    if (memcmp(record, magic, 8u) != 0 ||
        read_u32(record + 8u) != RECORD_FORMAT_VERSION ||
        read_u32(record + 20u) != expected_length ||
        expected_length > RECORD_PAYLOAD_CAPACITY ||
        read_u32(record + RECORD_SIZE - 4u) !=
            crc32(record, RECORD_SIZE - 4u)) {
        return FV_PERSIST_INVALID;
    }
    *sequence = read_u64(record + 12u);
    memcpy(payload, record + RECORD_HEADER_SIZE, expected_length);
    return FV_PERSIST_OK;
}

static fv_persist_result_t load_latest(const fv_host_services_context_t *context,
                                       const char *name, const uint8_t magic[8],
                                       uint8_t *payload, uint32_t payload_length,
                                       uint64_t *sequence) {
    bool found = false;
    bool invalid = false;
    uint64_t newest = 0u;
    uint8_t candidate[RECORD_PAYLOAD_CAPACITY];
    for (unsigned slot = 0u; slot < 2u; ++slot) {
        char path[FV_HOST_PATH_CAPACITY];
        if (!make_path(context, name, slot, path, sizeof(path))) {
            return FV_PERSIST_IO_ERROR;
        }
        uint64_t candidate_sequence = 0u;
        const fv_persist_result_t result = read_record(
            path, magic, candidate, payload_length, &candidate_sequence);
        if (result == FV_PERSIST_INVALID) invalid = true;
        if (result == FV_PERSIST_IO_ERROR) return result;
        if (result == FV_PERSIST_OK && (!found || candidate_sequence > newest)) {
            memcpy(payload, candidate, payload_length);
            newest = candidate_sequence;
            found = true;
        }
    }
    if (!found) return invalid ? FV_PERSIST_INVALID : FV_PERSIST_NOT_FOUND;
    *sequence = newest;
    return FV_PERSIST_OK;
}

static fv_persist_result_t store_record(
    const fv_host_services_context_t *context, const char *name,
    const uint8_t magic[8], const uint8_t *payload, uint32_t payload_length,
    uint64_t sequence) {
    if (payload_length > RECORD_PAYLOAD_CAPACITY) return FV_PERSIST_INVALID;
    uint8_t record[RECORD_SIZE] = {0};
    memcpy(record, magic, 8u);
    write_u32(record + 8u, RECORD_FORMAT_VERSION);
    write_u64(record + 12u, sequence);
    write_u32(record + 20u, payload_length);
    memcpy(record + RECORD_HEADER_SIZE, payload, payload_length);
    write_u32(record + RECORD_SIZE - 4u, crc32(record, RECORD_SIZE - 4u));

    char path[FV_HOST_PATH_CAPACITY];
    char temporary[FV_HOST_PATH_CAPACITY];
    const unsigned slot = (unsigned)(sequence & 1u);
    if (!make_path(context, name, slot, path, sizeof(path))) {
        return FV_PERSIST_IO_ERROR;
    }
    const int temp_result = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld",
                                     path, (long)getpid());
    if (temp_result < 0 || (size_t)temp_result >= sizeof(temporary)) {
        return FV_PERSIST_IO_ERROR;
    }
    const int descriptor = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                                S_IRUSR | S_IWUSR);
    if (descriptor < 0) return FV_PERSIST_IO_ERROR;
    bool ok = write_all(descriptor, record, sizeof(record));
    if (ok) ok = fsync(descriptor) == 0;
    if (close(descriptor) != 0) ok = false;
    if (ok) ok = rename(temporary, path) == 0;
    if (!ok) {
        (void)unlink(temporary);
        return FV_PERSIST_IO_ERROR;
    }
    const int directory = open(context->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory >= 0) {
        (void)fsync(directory);
        (void)close(directory);
    }
    return FV_PERSIST_OK;
}

static bool host_random_fill(fv_platform_services_t *services, uint8_t *output,
                             size_t length) {
    (void)services;
    if (output == NULL && length != 0u) return false;
    size_t filled = 0u;
    while (filled < length) {
        const ssize_t result = getrandom(output + filled, length - filled, 0u);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        filled += (size_t)result;
    }
    return true;
}

static fv_persist_result_t marker_status(
    const fv_host_services_context_t *context, const char *name,
    const uint8_t magic[8], uint32_t payload_size, bool *present) {
    uint8_t payload[SECRET_PAYLOAD_SIZE];
    uint64_t sequence = 0u;
    const fv_persist_result_t result = load_latest(
        context, name, magic, payload, payload_size, &sequence);
    (void)sequence;
    if (result == FV_PERSIST_OK) {
        *present = true;
        return FV_PERSIST_OK;
    }
    if (result == FV_PERSIST_NOT_FOUND) {
        *present = false;
        return FV_PERSIST_OK;
    }
    return result;
}

static fv_persist_result_t host_device_secret_status(
    fv_platform_services_t *services, fv_device_secret_status_t *status) {
    if (services == NULL || status == NULL) return FV_PERSIST_INVALID;
    const fv_host_services_context_t *context = services->context;
    bool revoked = false;
    fv_persist_result_t result = marker_status(
        context, "device-secret-revoked", REVOKE_MAGIC, 1u, &revoked);
    if (result != FV_PERSIST_OK) {
        *status = FV_DEVICE_SECRET_INVALID;
        return result;
    }
    if (revoked) {
        *status = FV_DEVICE_SECRET_REVOKED;
        return FV_PERSIST_OK;
    }
    bool secret_present = false;
    result = marker_status(context, "device-secret", SECRET_MAGIC,
                           SECRET_PAYLOAD_SIZE, &secret_present);
    if (result != FV_PERSIST_OK) {
        *status = FV_DEVICE_SECRET_INVALID;
        return result;
    }
    bool active_marker = false;
    result = marker_status(context, "device-secret-active", ACTIVE_MAGIC,
                           1u, &active_marker);
    if (result != FV_PERSIST_OK) {
        *status = FV_DEVICE_SECRET_INVALID;
        return result;
    }
    if (!secret_present && !active_marker) {
        *status = FV_DEVICE_SECRET_EMPTY;
    } else if (secret_present && active_marker) {
        *status = FV_DEVICE_SECRET_ACTIVE;
    } else {
        *status = FV_DEVICE_SECRET_INVALID;
    }
    return FV_PERSIST_OK;
}

static fv_persist_result_t host_provision_device_secret(
    fv_platform_services_t *services, const fv_device_secret_t *secret) {
    if (services == NULL || secret == NULL) return FV_PERSIST_INVALID;
    fv_device_secret_status_t status;
    const fv_persist_result_t result = host_device_secret_status(services, &status);
    if (result != FV_PERSIST_OK) return result;
    if (status != FV_DEVICE_SECRET_EMPTY) return FV_PERSIST_INVALID;
    fv_persist_result_t store_result = store_record(
        services->context, "device-secret", SECRET_MAGIC,
        secret->device_secret, FV_DEVICE_SECRET_SIZE, 1u);
    if (store_result != FV_PERSIST_OK) return store_result;

    fv_device_secret_t verified;
    uint64_t sequence = 0u;
    store_result = load_latest(services->context, "device-secret", SECRET_MAGIC,
                               verified.device_secret, FV_DEVICE_SECRET_SIZE,
                               &sequence);
    const bool verified_ok = store_result == FV_PERSIST_OK &&
        memcmp(verified.device_secret, secret->device_secret,
               FV_DEVICE_SECRET_SIZE) == 0;
    secure_clear(&verified, sizeof(verified));
    if (!verified_ok) {
        return FV_PERSIST_INVALID;
    }
    const uint8_t active = 1u;
    return store_record(services->context, "device-secret-active",
                        ACTIVE_MAGIC, &active, 1u, 1u);
}

static fv_persist_result_t host_read_device_secret(
    fv_platform_services_t *services, fv_device_secret_t *secret) {
    if (services == NULL || secret == NULL) return FV_PERSIST_INVALID;
    fv_device_secret_status_t status;
    fv_persist_result_t result = host_device_secret_status(services, &status);
    if (result != FV_PERSIST_OK) return result;
    if (status == FV_DEVICE_SECRET_EMPTY) return FV_PERSIST_NOT_FOUND;
    if (status != FV_DEVICE_SECRET_ACTIVE) return FV_PERSIST_INVALID;
    uint64_t sequence = 0u;
    result = load_latest(services->context, "device-secret", SECRET_MAGIC,
                         secret->device_secret, FV_DEVICE_SECRET_SIZE,
                         &sequence);
    (void)sequence;
    return result;
}

static fv_persist_result_t host_revoke_device_secret(
    fv_platform_services_t *services) {
    if (services == NULL) return FV_PERSIST_INVALID;
    fv_device_secret_status_t status;
    const fv_persist_result_t result = host_device_secret_status(services, &status);
    if (result != FV_PERSIST_OK) return result;
    if (status == FV_DEVICE_SECRET_REVOKED) return FV_PERSIST_OK;
    if (status != FV_DEVICE_SECRET_ACTIVE) return FV_PERSIST_INVALID;
    const uint8_t revoked = 1u;
    return store_record(services->context, "device-secret-revoked",
                        REVOKE_MAGIC, &revoked, 1u, 1u);
}

static fv_persist_result_t host_load_security_state(
    fv_platform_services_t *services, fv_security_state_t *state) {
    if (services == NULL || state == NULL) return FV_PERSIST_INVALID;
    uint8_t payload[SECURITY_PAYLOAD_SIZE];
    uint64_t sequence = 0u;
    const fv_persist_result_t result = load_latest(
        services->context, "security-state", SECURITY_MAGIC, payload,
        sizeof(payload), &sequence);
    if (result != FV_PERSIST_OK) return result;
    *state = (fv_security_state_t) {
        .sequence = sequence,
        .failed_attempts = payload[0],
        .provisioned = payload[1] == 1u,
    };
    return payload[1] <= 1u ? FV_PERSIST_OK : FV_PERSIST_INVALID;
}

static fv_persist_result_t host_store_security_state(
    fv_platform_services_t *services, const fv_security_state_t *state) {
    if (services == NULL || state == NULL) return FV_PERSIST_INVALID;
    uint8_t payload[SECURITY_PAYLOAD_SIZE] = {0};
    payload[0] = state->failed_attempts;
    payload[1] = state->provisioned ? 1u : 0u;
    return store_record(services->context, "security-state", SECURITY_MAGIC,
                        payload, sizeof(payload), state->sequence);
}

static fv_persist_result_t host_load_vault_header(
    fv_platform_services_t *services, fv_vault_header_t *header) {
    if (services == NULL || header == NULL) return FV_PERSIST_INVALID;
    uint8_t payload[VAULT_PAYLOAD_SIZE];
    uint64_t sequence = 0u;
    const fv_persist_result_t result = load_latest(
        services->context, "vault-header", VAULT_MAGIC, payload,
        sizeof(payload), &sequence);
    if (result != FV_PERSIST_OK) return result;
    *header = (fv_vault_header_t) {0};
    header->sequence = sequence;
    header->crypto_profile = (fv_crypto_profile_t)read_u32(payload);
    header->entry_method = read_u32(payload + 4u);
    memcpy(header->vault_id, payload + 8u, FV_VAULT_ID_SIZE);
    memcpy(header->branch_a_salt, payload + 24u, FV_SALT_SIZE);
    memcpy(header->branch_b_salt, payload + 40u, FV_SALT_SIZE);
    header->branch_a_cost = read_u32(payload + 56u);
    header->branch_b_cost = read_u32(payload + 60u);
    header->wrapped_vmk_length = read_u16(payload + 64u);
    if (header->wrapped_vmk_length > FV_WRAPPED_VMK_CAPACITY ||
        header->crypto_profile == FV_CRYPTO_PROFILE_UNAVAILABLE) {
        return FV_PERSIST_INVALID;
    }
    memcpy(header->wrapped_vmk, payload + 66u, FV_WRAPPED_VMK_CAPACITY);
    return FV_PERSIST_OK;
}

static fv_persist_result_t host_store_vault_header(
    fv_platform_services_t *services, const fv_vault_header_t *header) {
    if (services == NULL || header == NULL ||
        header->crypto_profile == FV_CRYPTO_PROFILE_UNAVAILABLE ||
        header->wrapped_vmk_length == 0u ||
        header->wrapped_vmk_length > FV_WRAPPED_VMK_CAPACITY) {
        return FV_PERSIST_INVALID;
    }
    uint8_t payload[VAULT_PAYLOAD_SIZE] = {0};
    write_u32(payload, (uint32_t)header->crypto_profile);
    write_u32(payload + 4u, header->entry_method);
    memcpy(payload + 8u, header->vault_id, FV_VAULT_ID_SIZE);
    memcpy(payload + 24u, header->branch_a_salt, FV_SALT_SIZE);
    memcpy(payload + 40u, header->branch_b_salt, FV_SALT_SIZE);
    write_u32(payload + 56u, header->branch_a_cost);
    write_u32(payload + 60u, header->branch_b_cost);
    write_u16(payload + 64u, header->wrapped_vmk_length);
    memcpy(payload + 66u, header->wrapped_vmk, FV_WRAPPED_VMK_CAPACITY);
    return store_record(services->context, "vault-header", VAULT_MAGIC,
                        payload, sizeof(payload), header->sequence);
}

static const fv_platform_service_ops_t HOST_OPS = {
    .random_fill = host_random_fill,
    .device_secret_status = host_device_secret_status,
    .provision_device_secret = host_provision_device_secret,
    .read_device_secret = host_read_device_secret,
    .revoke_device_secret = host_revoke_device_secret,
    .load_security_state = host_load_security_state,
    .store_security_state = host_store_security_state,
    .load_vault_header = host_load_vault_header,
    .store_vault_header = host_store_vault_header,
};

bool fv_host_services_init(fv_platform_services_t *services,
                           fv_host_services_context_t *context,
                           const char *directory) {
    if (services == NULL || context == NULL || directory == NULL ||
        directory[0] == '\0') return false;
    const size_t length = strlen(directory);
    if (length >= sizeof(context->directory)) return false;
    memcpy(context->directory, directory, length + 1u);
    if (mkdir(directory, S_IRWXU) != 0 && errno != EEXIST) return false;
    struct stat status;
    if (stat(directory, &status) != 0 || !S_ISDIR(status.st_mode) ||
        (status.st_mode & (S_IRWXG | S_IRWXO)) != 0u) return false;
    services->ops = &HOST_OPS;
    services->context = context;
    return true;
}
