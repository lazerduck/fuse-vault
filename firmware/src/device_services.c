#include "fuse_vault/device_services.h"

#include "fuse_vault/block_slice.h"
#include "fuse_vault/journal_authenticator.h"
#include "fuse_vault/media_layout.h"
#include "fuse_vault/vault_header_store.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

static fv_device_services_context_t *get_context(
    fv_platform_services_t *services) {
    return services != NULL ? services->context : NULL;
}

static fv_persist_result_t roots_result(fv_device_roots_result_t result) {
    switch (result) {
        case FV_DEVICE_ROOTS_OK:
        case FV_DEVICE_ROOTS_ACTIVE:
            return FV_PERSIST_OK;
        case FV_DEVICE_ROOTS_EMPTY:
            return FV_PERSIST_NOT_FOUND;
        case FV_DEVICE_ROOTS_IO_ERROR:
            return FV_PERSIST_IO_ERROR;
        case FV_DEVICE_ROOTS_REVOKED:
        case FV_DEVICE_ROOTS_INVALID:
        case FV_DEVICE_ROOTS_NOT_PERMITTED:
            return FV_PERSIST_INVALID;
    }
    return FV_PERSIST_INVALID;
}

static bool random_fill(fv_platform_services_t *services, uint8_t *output,
                        size_t length) {
    fv_device_services_context_t *context = get_context(services);
    return context != NULL && context->random_fill != NULL &&
           context->random_fill(context->random_context, output, length);
}

static fv_persist_result_t device_secret_status(
    fv_platform_services_t *services, fv_device_secret_status_t *status) {
    fv_device_services_context_t *context = get_context(services);
    if (context == NULL || status == NULL) return FV_PERSIST_INVALID;
    switch (fv_device_roots_status(context->roots_storage)) {
        case FV_DEVICE_ROOTS_EMPTY:
            *status = FV_DEVICE_SECRET_EMPTY;
            return FV_PERSIST_OK;
        case FV_DEVICE_ROOTS_ACTIVE:
            *status = FV_DEVICE_SECRET_ACTIVE;
            return FV_PERSIST_OK;
        case FV_DEVICE_ROOTS_REVOKED:
            *status = FV_DEVICE_SECRET_REVOKED;
            return FV_PERSIST_OK;
        case FV_DEVICE_ROOTS_IO_ERROR:
            *status = FV_DEVICE_SECRET_INVALID;
            return FV_PERSIST_IO_ERROR;
        case FV_DEVICE_ROOTS_OK:
        case FV_DEVICE_ROOTS_INVALID:
        case FV_DEVICE_ROOTS_NOT_PERMITTED:
            *status = FV_DEVICE_SECRET_INVALID;
            return FV_PERSIST_INVALID;
    }
    *status = FV_DEVICE_SECRET_INVALID;
    return FV_PERSIST_INVALID;
}

static fv_persist_result_t provision_device_secret(
    fv_platform_services_t *services, const fv_device_secret_t *secret) {
    fv_device_services_context_t *context = get_context(services);
    if (context == NULL || secret == NULL) return FV_PERSIST_INVALID;
    return roots_result(
        fv_device_roots_provision(context->roots_storage, secret));
}

static fv_persist_result_t read_device_secret(
    fv_platform_services_t *services, fv_device_secret_t *secret) {
    fv_device_services_context_t *context = get_context(services);
    if (context == NULL || secret == NULL) return FV_PERSIST_INVALID;
    return roots_result(fv_device_roots_read(context->roots_storage, secret));
}

static fv_persist_result_t revoke_device_secret(
    fv_platform_services_t *services) {
    fv_device_services_context_t *context = get_context(services);
    if (context == NULL) return FV_PERSIST_INVALID;
    return roots_result(fv_device_roots_revoke(context->roots_storage));
}

static fv_persist_result_t open_journal(
    fv_device_services_context_t *context,
    fv_dual_journal_authenticator_t *authenticator,
    fv_security_journal_t *journal) {
    fv_device_secret_t roots = {0};
    const fv_device_roots_result_t read_result =
        fv_device_roots_read(context->roots_storage, &roots);
    if (read_result != FV_DEVICE_ROOTS_ACTIVE) {
        secure_clear(&roots, sizeof(roots));
        return roots_result(read_result);
    }
    const bool ok = fv_dual_journal_authenticator_init(
                        authenticator, &roots, context->device_id) &&
                    fv_security_journal_init(
                        journal, context->journal_flash,
                        &authenticator->interface);
    secure_clear(&roots, sizeof(roots));
    if (!ok) {
        fv_dual_journal_authenticator_deinit(authenticator);
        return FV_PERSIST_INVALID;
    }
    return FV_PERSIST_OK;
}

static fv_persist_result_t load_security_state(
    fv_platform_services_t *services, fv_security_state_t *state) {
    fv_device_services_context_t *context = get_context(services);
    if (context == NULL || state == NULL) return FV_PERSIST_INVALID;
    fv_dual_journal_authenticator_t authenticator = {0};
    fv_security_journal_t journal;
    const fv_persist_result_t opened =
        open_journal(context, &authenticator, &journal);
    if (opened != FV_PERSIST_OK) return opened;
    fv_journal_state_t recovered;
    const fv_journal_result_t result =
        fv_security_journal_recover(&journal, &recovered);
    fv_dual_journal_authenticator_deinit(&authenticator);
    if (result == FV_JOURNAL_EMPTY) return FV_PERSIST_NOT_FOUND;
    if (result != FV_JOURNAL_OK) {
        secure_clear(&recovered, sizeof(recovered));
        return result == FV_JOURNAL_IO_ERROR ? FV_PERSIST_IO_ERROR
                                             : FV_PERSIST_INVALID;
    }
    *state = (fv_security_state_t) {
        .sequence = recovered.sequence,
        .failed_attempts = recovered.failed_attempts,
        .provisioned = recovered.provisioned,
        .fido_initialized = recovered.fido_initialized,
    };
    memcpy(state->fido_digest, recovered.fido_digest, sizeof(state->fido_digest));
    memcpy(context->current_vault_id, recovered.vault_id,
           FV_VAULT_ID_SIZE);
    context->current_vault_id_valid = true;
    secure_clear(&recovered, sizeof(recovered));
    return FV_PERSIST_OK;
}

static fv_persist_result_t store_security_state(
    fv_platform_services_t *services, const fv_security_state_t *state) {
    fv_device_services_context_t *context = get_context(services);
    if (context == NULL || state == NULL ||
        !context->current_vault_id_valid || state->sequence == 0u) {
        return FV_PERSIST_INVALID;
    }
    fv_dual_journal_authenticator_t authenticator = {0};
    fv_security_journal_t journal;
    const fv_persist_result_t opened =
        open_journal(context, &authenticator, &journal);
    if (opened != FV_PERSIST_OK) return opened;

    fv_journal_state_t previous = {0};
    const fv_journal_result_t recovered =
        fv_security_journal_recover(&journal, &previous);
    const bool sequence_valid =
        (recovered == FV_JOURNAL_EMPTY && state->sequence == 1u) ||
        (recovered == FV_JOURNAL_OK && previous.sequence != UINT64_MAX &&
         state->sequence == previous.sequence + 1u &&
         memcmp(previous.vault_id, context->current_vault_id,
                FV_VAULT_ID_SIZE) == 0);
    if (!sequence_valid) {
        fv_dual_journal_authenticator_deinit(&authenticator);
        secure_clear(&previous, sizeof(previous));
        return recovered == FV_JOURNAL_IO_ERROR ? FV_PERSIST_IO_ERROR
                                                : FV_PERSIST_INVALID;
    }
    fv_journal_state_t next = {
        .sequence = state->sequence,
        .previous_sequence = recovered == FV_JOURNAL_OK
            ? previous.sequence : 0u,
        .failed_attempts = state->failed_attempts,
        .provisioned = state->provisioned,
    };
    next.fido_initialized = state->fido_initialized;
    memcpy(next.fido_digest, state->fido_digest, sizeof(next.fido_digest));
    memcpy(next.vault_id, context->current_vault_id, FV_VAULT_ID_SIZE);
    const fv_journal_result_t append_result =
        fv_security_journal_append(&journal, &next);
    fv_dual_journal_authenticator_deinit(&authenticator);
    secure_clear(&previous, sizeof(previous));
    secure_clear(&next, sizeof(next));
    return append_result == FV_JOURNAL_OK ? FV_PERSIST_OK
        : append_result == FV_JOURNAL_IO_ERROR ? FV_PERSIST_IO_ERROR
                                               : FV_PERSIST_INVALID;
}

static fv_persist_result_t load_vault_header(
    fv_platform_services_t *services, fv_vault_header_t *header) {
    fv_device_services_context_t *context = get_context(services);
    if (context == NULL || header == NULL) return FV_PERSIST_INVALID;
    fv_device_secret_t roots = {0};
    fv_persist_result_t result = roots_result(
        fv_device_roots_read(context->roots_storage, &roots));
    if (result != FV_PERSIST_OK) return result;
    fv_media_layout_t layout;
    const fv_media_result_t media_result = fv_media_load(
        context->raw_media, &roots,
        context->current_vault_id_valid ? context->current_vault_id : NULL,
        &layout);
    fv_block_slice_t header_slice;
    if (media_result != FV_MEDIA_OK ||
        !fv_media_open_header(&layout, context->raw_media, &header_slice)) {
        secure_clear(&roots, sizeof(roots));
        return media_result == FV_MEDIA_BLANK ? FV_PERSIST_NOT_FOUND
            : media_result == FV_MEDIA_IO_ERROR ? FV_PERSIST_IO_ERROR
                                                : FV_PERSIST_INVALID;
    }
    const fv_vault_header_store_result_t header_result =
        fv_vault_header_store_load(&header_slice.interface, &roots,
                                   layout.vault_id, header);
    if (header_result == FV_VAULT_HEADER_STORE_OK) {
        memcpy(context->current_vault_id, header->vault_id,
               FV_VAULT_ID_SIZE);
        context->current_vault_id_valid = true;
    }
    secure_clear(&roots, sizeof(roots));
    if (header_result == FV_VAULT_HEADER_STORE_OK) return FV_PERSIST_OK;
    if (header_result == FV_VAULT_HEADER_STORE_NOT_FOUND) {
        return FV_PERSIST_NOT_FOUND;
    }
    return header_result == FV_VAULT_HEADER_STORE_IO_ERROR
        ? FV_PERSIST_IO_ERROR : FV_PERSIST_INVALID;
}

static fv_persist_result_t store_vault_header(
    fv_platform_services_t *services, const fv_vault_header_t *header) {
    fv_device_services_context_t *context = get_context(services);
    if (context == NULL || header == NULL) return FV_PERSIST_INVALID;
    fv_device_secret_t roots = {0};
    fv_persist_result_t result = roots_result(
        fv_device_roots_read(context->roots_storage, &roots));
    if (result != FV_PERSIST_OK) return result;

    fv_media_layout_t layout;
    fv_media_result_t media_result = fv_media_load(
        context->raw_media, &roots, header->vault_id, &layout);
    if (media_result == FV_MEDIA_BLANK) {
        media_result = fv_media_format(
            context->raw_media, &roots, header->vault_id,
            FV_MEDIA_DEFAULT_FIDO_BLOCKS, &layout);
    }
    fv_block_slice_t header_slice;
    if (media_result != FV_MEDIA_OK ||
        !fv_media_open_header(&layout, context->raw_media, &header_slice)) {
        secure_clear(&roots, sizeof(roots));
        return media_result == FV_MEDIA_IO_ERROR ? FV_PERSIST_IO_ERROR
                                                 : FV_PERSIST_INVALID;
    }
    const fv_vault_header_store_result_t header_result =
        fv_vault_header_store_update(&header_slice.interface, &roots, header);
    secure_clear(&roots, sizeof(roots));
    if (header_result != FV_VAULT_HEADER_STORE_OK) {
        return header_result == FV_VAULT_HEADER_STORE_IO_ERROR
            ? FV_PERSIST_IO_ERROR : FV_PERSIST_INVALID;
    }
    memcpy(context->current_vault_id, header->vault_id, FV_VAULT_ID_SIZE);
    context->current_vault_id_valid = true;
    return FV_PERSIST_OK;
}

static const fv_platform_service_ops_t OPS = {
    .random_fill = random_fill,
    .device_secret_status = device_secret_status,
    .provision_device_secret = provision_device_secret,
    .read_device_secret = read_device_secret,
    .revoke_device_secret = revoke_device_secret,
    .load_security_state = load_security_state,
    .store_security_state = store_security_state,
    .load_vault_header = load_vault_header,
    .store_vault_header = store_vault_header,
};

bool fv_device_services_init(
    fv_platform_services_t *services, fv_device_services_context_t *context,
    fv_device_roots_storage_t *roots_storage,
    fv_journal_flash_t *journal_flash, fv_block_device_t *raw_media,
    const uint8_t device_id[FV_VAULT_ID_SIZE],
    fv_device_services_random_fill_fn random_fill_callback,
    void *random_context) {
    if (services == NULL || context == NULL || roots_storage == NULL ||
        journal_flash == NULL || raw_media == NULL || device_id == NULL ||
        random_fill_callback == NULL) {
        return false;
    }
    *context = (fv_device_services_context_t) {
        .roots_storage = roots_storage,
        .journal_flash = journal_flash,
        .raw_media = raw_media,
        .random_fill = random_fill_callback,
        .random_context = random_context,
    };
    memcpy(context->device_id, device_id, FV_VAULT_ID_SIZE);
    services->ops = &OPS;
    services->context = context;
    return true;
}
