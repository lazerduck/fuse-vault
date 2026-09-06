#define _GNU_SOURCE

#include "host_services.h"
#include "fuse_vault/journal_authenticator.h"
#include "fuse_vault/vault_header_store.h"
#include "fuse_vault/media_layout.h"

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
#define JOURNAL_SIZE 8192u
#define JOURNAL_ERASE_SIZE 4096u

static const uint8_t SECRET_MAGIC[8] = {'F','V','S','E','C','D','1',0};
static const uint8_t ACTIVE_MAGIC[8] = {'F','V','A','C','T','D','1',0};
static const uint8_t REVOKE_MAGIC[8] = {'F','V','R','E','V','D','1',0};
static const uint8_t JOURNAL_DEVICE_ID[FV_VAULT_ID_SIZE] = {
    0x46,0x56,0x48,0x4f,0x53,0x54,0x4a,0x4f,
    0x55,0x52,0x4e,0x41,0x4c,0x30,0x30,0x31
};

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

static bool journal_transfer(fv_journal_flash_t *flash, size_t offset,
                             uint8_t *data, size_t length, bool programming) {
    fv_host_services_context_t *context = flash->context;
    if (offset > flash->size || length > flash->size - offset) return false;
    const int descriptor = open(context->journal_path,
        (programming ? O_RDWR : O_RDONLY) | O_CLOEXEC);
    if (descriptor < 0) return false;
    uint8_t existing[FV_JOURNAL_RECORD_SIZE];
    if (length > sizeof(existing)) { (void)close(descriptor); return false; }
    if (programming && pread(descriptor,existing,length,(off_t)offset)!=(ssize_t)length) {
        (void)close(descriptor); return false;
    }
    if (programming) {
        for (size_t index = 0u; index < length; ++index) {
            if ((existing[index] & data[index]) != data[index]) {
                (void)close(descriptor); return false;
            }
        }
    }
    size_t done = 0u;
    while (done < length) {
        const ssize_t amount = programming
            ? pwrite(descriptor, data + done, length - done,
                     (off_t)(offset + done))
            : pread(descriptor, data + done, length - done,
                    (off_t)(offset + done));
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) { (void)close(descriptor); return false; }
        done += (size_t)amount;
    }
    const bool ok = (!programming || fdatasync(descriptor) == 0) &&
                    close(descriptor) == 0;
    secure_clear(existing, sizeof(existing));
    return ok;
}
static bool journal_read(fv_journal_flash_t *flash,size_t offset,
                         uint8_t *output,size_t length){
    return output != NULL && journal_transfer(flash,offset,output,length,false);
}
static bool journal_program(fv_journal_flash_t *flash,size_t offset,
                            const uint8_t *input,size_t length){
    return input != NULL && journal_transfer(flash,offset,(uint8_t *)input,length,true);
}
static bool journal_erase(fv_journal_flash_t *flash,size_t offset,size_t length){
    fv_host_services_context_t *context=flash->context;
    if(offset%flash->erase_block_size!=0u||length%flash->erase_block_size!=0u||
       offset>flash->size||length>flash->size-offset)return false;
    int descriptor=open(context->journal_path,O_RDWR|O_CLOEXEC);if(descriptor<0)return false;
    uint8_t erased[256];memset(erased,0xff,sizeof(erased));bool ok=true;
    for(size_t position=0u;ok&&position<length;position+=sizeof(erased))
        ok=pwrite(descriptor,erased,sizeof(erased),(off_t)(offset+position))==(ssize_t)sizeof(erased);
    if(ok)ok=fdatasync(descriptor)==0;
    if(close(descriptor)!=0)ok=false;
    return ok;
}
static const fv_journal_flash_ops_t JOURNAL_OPS={journal_read,journal_program,journal_erase};

static fv_persist_result_t open_journal(fv_platform_services_t *services,
                                        fv_security_journal_t *journal,
                                        fv_dual_journal_authenticator_t *auth) {
    fv_device_secret_t roots={0};
    const fv_persist_result_t result=host_read_device_secret(services,&roots);
    if(result!=FV_PERSIST_OK)return result;
    bool ok=fv_dual_journal_authenticator_init(auth,&roots,JOURNAL_DEVICE_ID)&&
            fv_security_journal_init(journal,
                &((fv_host_services_context_t *)services->context)->journal_flash,
                &auth->interface);
    secure_clear(&roots,sizeof(roots));return ok?FV_PERSIST_OK:FV_PERSIST_IO_ERROR;
}

static fv_persist_result_t host_load_security_state(
    fv_platform_services_t *services, fv_security_state_t *state) {
    if (services == NULL || state == NULL) return FV_PERSIST_INVALID;
    fv_security_journal_t journal;fv_dual_journal_authenticator_t auth;
    fv_persist_result_t opened=open_journal(services,&journal,&auth);
    if(opened!=FV_PERSIST_OK)return opened;
    fv_journal_state_t recovered;
    const fv_journal_result_t result=fv_security_journal_recover(&journal,&recovered);
    fv_dual_journal_authenticator_deinit(&auth);
    if(result==FV_JOURNAL_EMPTY)return FV_PERSIST_NOT_FOUND;
    if(result!=FV_JOURNAL_OK)return result==FV_JOURNAL_IO_ERROR?FV_PERSIST_IO_ERROR:FV_PERSIST_INVALID;
    *state = (fv_security_state_t) {
        .sequence = recovered.sequence,
        .failed_attempts = recovered.failed_attempts,
        .provisioned = recovered.provisioned,
        .fido_initialized = recovered.fido_initialized,
    };
    memcpy(state->fido_digest, recovered.fido_digest, sizeof(state->fido_digest));
    state->header_sequence = recovered.header_sequence;
    memcpy(state->header_tag, recovered.header_tag, sizeof(state->header_tag));
    fv_host_services_context_t *context=services->context;
    memcpy(context->current_vault_id,recovered.vault_id,FV_VAULT_ID_SIZE);
    context->current_vault_id_valid=true;
    secure_clear(&recovered,sizeof(recovered));return FV_PERSIST_OK;
}

static fv_persist_result_t host_store_security_state(
    fv_platform_services_t *services, const fv_security_state_t *state) {
    if (services == NULL || state == NULL) return FV_PERSIST_INVALID;
    fv_host_services_context_t *context=services->context;
    if(!context->current_vault_id_valid)return FV_PERSIST_INVALID;
    fv_security_journal_t journal;fv_dual_journal_authenticator_t auth;
    fv_persist_result_t opened=open_journal(services,&journal,&auth);
    if(opened!=FV_PERSIST_OK)return opened;
    fv_journal_state_t previous;fv_journal_result_t recovered=fv_security_journal_recover(&journal,&previous);
    fv_journal_state_t next={.sequence=state->sequence,
        .previous_sequence=recovered==FV_JOURNAL_OK?previous.sequence:0u,
        .failed_attempts=state->failed_attempts,.provisioned=state->provisioned};
    next.header_sequence = state->header_sequence;
    memcpy(next.header_tag, state->header_tag, sizeof(next.header_tag));
    next.fido_initialized = state->fido_initialized;
    memcpy(next.fido_digest, state->fido_digest, sizeof(next.fido_digest));
    memcpy(next.vault_id,context->current_vault_id,FV_VAULT_ID_SIZE);
    fv_journal_result_t appended=(recovered==FV_JOURNAL_OK||recovered==FV_JOURNAL_EMPTY)
        ?fv_security_journal_append(&journal,&next):recovered;
    fv_dual_journal_authenticator_deinit(&auth);secure_clear(&previous,sizeof(previous));
    return appended==FV_JOURNAL_OK?FV_PERSIST_OK:
        appended==FV_JOURNAL_IO_ERROR?FV_PERSIST_IO_ERROR:FV_PERSIST_INVALID;
}

static fv_persist_result_t host_load_vault_header(
    fv_platform_services_t *services, fv_vault_header_t *header) {
    if (services == NULL || header == NULL) return FV_PERSIST_INVALID;
    fv_security_state_t state = {0};
    const fv_persist_result_t state_result = host_load_security_state(services, &state);
    if (state_result != FV_PERSIST_OK && state_result != FV_PERSIST_NOT_FOUND)
        return state_result;
    fv_device_secret_t roots = {0};
    fv_persist_result_t rr = host_read_device_secret(services, &roots);
    if (rr != FV_PERSIST_OK) return rr;
    fv_host_services_context_t *context = services->context;
    fv_media_layout_t layout;
    fv_block_slice_t header_slice;
    const fv_media_result_t media_result = fv_media_load(
        &context->vault_device, &roots,
        context->current_vault_id_valid ? context->current_vault_id : NULL, &layout);
    if (media_result != FV_MEDIA_OK ||
        !fv_media_open_header(&layout, &context->vault_device, &header_slice)) {
        secure_clear(&roots, sizeof(roots));
        return media_result == FV_MEDIA_BLANK ? FV_PERSIST_NOT_FOUND
            : media_result == FV_MEDIA_IO_ERROR ? FV_PERSIST_IO_ERROR
                                                : FV_PERSIST_INVALID;
    }
    const fv_vault_header_store_result_t result = fv_vault_header_store_load_committed(
        &header_slice.interface, &roots, layout.vault_id, &state, header);
    if (result == FV_VAULT_HEADER_STORE_OK) {
        memcpy(context->current_vault_id,header->vault_id,FV_VAULT_ID_SIZE);
        context->current_vault_id_valid=true;
    }
    secure_clear(&roots, sizeof(roots));
    if (result == FV_VAULT_HEADER_STORE_OK) return FV_PERSIST_OK;
    if (result == FV_VAULT_HEADER_STORE_NOT_FOUND) return FV_PERSIST_NOT_FOUND;
    return result == FV_VAULT_HEADER_STORE_IO_ERROR ? FV_PERSIST_IO_ERROR
                                                    : FV_PERSIST_INVALID;
}

static fv_persist_result_t host_store_vault_header(
    fv_platform_services_t *services, const fv_vault_header_t *header) {
    if (services == NULL || header == NULL) return FV_PERSIST_INVALID;
    fv_security_state_t state = {0};
    const fv_persist_result_t state_result = host_load_security_state(services, &state);
    if (state_result != FV_PERSIST_OK && state_result != FV_PERSIST_NOT_FOUND)
        return state_result;
    fv_device_secret_t roots = {0};
    fv_persist_result_t rr = host_read_device_secret(services, &roots);
    if (rr != FV_PERSIST_OK) return rr;
    fv_host_services_context_t *context = services->context;
    memcpy(context->current_vault_id,header->vault_id,FV_VAULT_ID_SIZE);
    context->current_vault_id_valid=true;
    fv_media_layout_t layout;
    fv_media_result_t media_result = fv_media_load(
        &context->vault_device, &roots, header->vault_id, &layout);
    if (media_result == FV_MEDIA_BLANK) {
        media_result = fv_media_format(&context->vault_device, &roots,
                                       header->vault_id, context->fido_blocks, &layout);
    }
    fv_block_slice_t header_slice;
    if (media_result != FV_MEDIA_OK ||
        !fv_media_open_header(&layout, &context->vault_device, &header_slice)) {
        secure_clear(&roots, sizeof(roots));
        return media_result == FV_MEDIA_IO_ERROR ? FV_PERSIST_IO_ERROR
                                                 : FV_PERSIST_INVALID;
    }
    const fv_vault_header_store_result_t result = fv_vault_header_store_stage(
        &header_slice.interface, &roots, &state, header);
    secure_clear(&roots, sizeof(roots));
    return result == FV_VAULT_HEADER_STORE_OK ? FV_PERSIST_OK
        : result == FV_VAULT_HEADER_STORE_IO_ERROR ? FV_PERSIST_IO_ERROR
                                                  : FV_PERSIST_INVALID;
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

bool fv_host_services_init_sized(fv_platform_services_t *services,
                           fv_host_services_context_t *context,
                           const char *directory, uint64_t media_blocks, uint64_t fido_blocks) {
    if (services == NULL || context == NULL || directory == NULL ||
        directory[0] == '\0') return false;
    const size_t length = strlen(directory);
    if (length >= sizeof(context->directory)) return false;
    memcpy(context->directory, directory, length + 1u);
    context->current_vault_id_valid=false;
    if (mkdir(directory, S_IRWXU) != 0 && errno != EEXIST) return false;
    struct stat status;
    if (stat(directory, &status) != 0 || !S_ISDIR(status.st_mode) ||
        (status.st_mode & (S_IRWXG | S_IRWXO)) != 0u) return false;
    context->fido_blocks = fido_blocks;
    char media_path[FV_HOST_PATH_CAPACITY];
    const int media_result = snprintf(media_path, sizeof(media_path),
                                      "%s/vault-media.bin", directory);
    if (media_result < 0 || (size_t)media_result >= sizeof(media_path) ||
        !fv_host_file_block_device_init(&context->vault_device,
                                        &context->vault_device_context,
                                        media_path, media_blocks)) return false;
    const int journal_result=snprintf(context->journal_path,sizeof(context->journal_path),
                                      "%s/security-journal.bin",directory);
    if(journal_result<0||(size_t)journal_result>=sizeof(context->journal_path))return false;
    int journal_fd=open(context->journal_path,O_RDWR|O_CREAT|O_CLOEXEC,S_IRUSR|S_IWUSR);
    if(journal_fd<0)return false;
    struct stat journal_status;
    bool journal_ok=fstat(journal_fd,&journal_status)==0;
    if(journal_ok&&journal_status.st_size==0){uint8_t erased[256];memset(erased,0xff,sizeof(erased));
        for(size_t offset=0u;journal_ok&&offset<JOURNAL_SIZE;offset+=sizeof(erased))
            journal_ok=write(journal_fd,erased,sizeof(erased))==(ssize_t)sizeof(erased);
        if(journal_ok)journal_ok=fsync(journal_fd)==0;
    }else if(journal_ok)journal_ok=journal_status.st_size==(off_t)JOURNAL_SIZE;
    if(close(journal_fd)!=0)journal_ok=false;
    if(!journal_ok)return false;
    context->journal_flash=(fv_journal_flash_t){.ops=&JOURNAL_OPS,.context=context,
        .size=JOURNAL_SIZE,.erase_block_size=JOURNAL_ERASE_SIZE,
        .program_size=FV_JOURNAL_RECORD_SIZE};
    services->ops = &HOST_OPS;
    services->context = context;
    return true;
}

bool fv_host_services_init(fv_platform_services_t *services,
    fv_host_services_context_t *context, const char *directory) {
    return fv_host_services_init_sized(services, context, directory, 256u, 16u);
}
