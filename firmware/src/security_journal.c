#include "fuse_vault/security_journal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TAG_A_OFFSET FV_JOURNAL_AUTHENTICATED_SIZE
#define TAG_B_OFFSET (TAG_A_OFFSET + FV_JOURNAL_TAG_SIZE)
#define JOURNAL_FORMAT_VERSION 1u

static const uint8_t JOURNAL_MAGIC[8] = {'F','V','J','R','N','L','1',0};

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

static bool constant_time_equal(const uint8_t *left, const uint8_t *right,
                                size_t length) {
    uint8_t difference = 0u;
    for (size_t index = 0u; index < length; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0u;
}

static bool is_erased(const uint8_t record[FV_JOURNAL_RECORD_SIZE]) {
    uint8_t combined = 0xffu;
    for (size_t index = 0u; index < FV_JOURNAL_RECORD_SIZE; ++index) {
        combined &= record[index];
    }
    return combined == 0xffu;
}

static bool layout_is_valid(const fv_security_journal_t *journal) {
    if (journal == NULL || journal->flash == NULL ||
        journal->authenticator == NULL || journal->flash->ops == NULL ||
        journal->authenticator->ops == NULL ||
        journal->flash->ops->read == NULL ||
        journal->flash->ops->program == NULL ||
        journal->flash->ops->erase == NULL ||
        journal->authenticator->ops->compute_tags == NULL) return false;
    const fv_journal_flash_t *flash = journal->flash;
    return flash->program_size == FV_JOURNAL_RECORD_SIZE &&
           flash->erase_block_size >= FV_JOURNAL_RECORD_SIZE &&
           flash->erase_block_size % FV_JOURNAL_RECORD_SIZE == 0u &&
           flash->size == flash->erase_block_size *
                          FV_JOURNAL_MINIMUM_ERASE_BLOCKS;
}

static fv_journal_result_t decode_record(
    fv_security_journal_t *journal,
    const uint8_t record[FV_JOURNAL_RECORD_SIZE],
    fv_journal_state_t *state) {
    if (memcmp(record, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) != 0 ||
        read_u32(record + 8u) != JOURNAL_FORMAT_VERSION ||
        read_u32(record + 12u) != FV_JOURNAL_RECORD_SIZE ||
        record[49] > 1u) return FV_JOURNAL_AUTHENTICATION_ERROR;

    uint8_t expected_a[FV_JOURNAL_TAG_SIZE];
    uint8_t expected_b[FV_JOURNAL_TAG_SIZE];
    if (!journal->authenticator->ops->compute_tags(
            journal->authenticator, record, FV_JOURNAL_AUTHENTICATED_SIZE,
            expected_a, expected_b)) return FV_JOURNAL_IO_ERROR;
    const bool valid = constant_time_equal(expected_a, record + TAG_A_OFFSET,
                                           FV_JOURNAL_TAG_SIZE) &&
                       constant_time_equal(expected_b, record + TAG_B_OFFSET,
                                           FV_JOURNAL_TAG_SIZE);
    if (!valid) return FV_JOURNAL_AUTHENTICATION_ERROR;

    *state = (fv_journal_state_t) {
        .sequence = read_u64(record + 16u),
        .previous_sequence = read_u64(record + 24u),
        .failed_attempts = record[48],
        .provisioned = record[49] == 1u,
    };
    memcpy(state->vault_id, record + 32u, FV_VAULT_ID_SIZE);
    return state->sequence == 0u ? FV_JOURNAL_SEQUENCE_ERROR : FV_JOURNAL_OK;
}

static fv_journal_result_t scan(
    fv_security_journal_t *journal, fv_journal_state_t *latest,
    size_t *latest_offset, size_t *first_erased_offset) {
    bool found = false;
    bool duplicate = false;
    uint8_t record[FV_JOURNAL_RECORD_SIZE];
    *first_erased_offset = SIZE_MAX;
    *latest_offset = SIZE_MAX;
    for (size_t offset = 0u; offset < journal->flash->size;
         offset += FV_JOURNAL_RECORD_SIZE) {
        if (!journal->flash->ops->read(journal->flash, offset, record,
                                       sizeof(record))) {
            return FV_JOURNAL_IO_ERROR;
        }
        if (is_erased(record)) {
            if (*first_erased_offset == SIZE_MAX) *first_erased_offset = offset;
            continue;
        }
        fv_journal_state_t candidate;
        if (decode_record(journal, record, &candidate) != FV_JOURNAL_OK) continue;
        if (found && candidate.sequence == latest->sequence) duplicate = true;
        if (!found || candidate.sequence > latest->sequence) {
            *latest = candidate;
            *latest_offset = offset;
            found = true;
            duplicate = false;
        }
    }
    if (duplicate) return FV_JOURNAL_SEQUENCE_ERROR;
    return found ? FV_JOURNAL_OK : FV_JOURNAL_EMPTY;
}

static bool encode_record(fv_security_journal_t *journal,
                          const fv_journal_state_t *state,
                          uint8_t record[FV_JOURNAL_RECORD_SIZE]) {
    memset(record, 0xff, FV_JOURNAL_RECORD_SIZE);
    memcpy(record, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC));
    write_u32(record + 8u, JOURNAL_FORMAT_VERSION);
    write_u32(record + 12u, FV_JOURNAL_RECORD_SIZE);
    write_u64(record + 16u, state->sequence);
    write_u64(record + 24u, state->previous_sequence);
    memcpy(record + 32u, state->vault_id, FV_VAULT_ID_SIZE);
    record[48] = state->failed_attempts;
    record[49] = state->provisioned ? 1u : 0u;
    memset(record + 50u, 0, FV_JOURNAL_AUTHENTICATED_SIZE - 50u);
    return journal->authenticator->ops->compute_tags(
        journal->authenticator, record, FV_JOURNAL_AUTHENTICATED_SIZE,
        record + TAG_A_OFFSET, record + TAG_B_OFFSET);
}

bool fv_security_journal_init(fv_security_journal_t *journal,
                              fv_journal_flash_t *flash,
                              fv_journal_authenticator_t *authenticator) {
    if (journal == NULL) return false;
    *journal = (fv_security_journal_t) {
        .flash = flash,
        .authenticator = authenticator,
    };
    return layout_is_valid(journal);
}

fv_journal_result_t fv_security_journal_recover(
    fv_security_journal_t *journal, fv_journal_state_t *state) {
    if (!layout_is_valid(journal) || state == NULL) {
        return FV_JOURNAL_INVALID_ARGUMENT;
    }
    size_t latest_offset;
    size_t erased_offset;
    return scan(journal, state, &latest_offset, &erased_offset);
}

fv_journal_result_t fv_security_journal_append(
    fv_security_journal_t *journal, const fv_journal_state_t *state) {
    if (!layout_is_valid(journal) || state == NULL || state->sequence == 0u) {
        return FV_JOURNAL_INVALID_ARGUMENT;
    }

    fv_journal_state_t latest;
    size_t latest_offset;
    size_t erased_offset;
    const fv_journal_result_t recovered = scan(
        journal, &latest, &latest_offset, &erased_offset);
    if (recovered != FV_JOURNAL_OK && recovered != FV_JOURNAL_EMPTY) {
        return recovered;
    }
    if (recovered == FV_JOURNAL_EMPTY) {
        if (state->sequence != 1u || state->previous_sequence != 0u) {
            return FV_JOURNAL_SEQUENCE_ERROR;
        }
    } else if (state->sequence != latest.sequence + 1u ||
               state->previous_sequence != latest.sequence ||
               memcmp(state->vault_id, latest.vault_id,
                      FV_VAULT_ID_SIZE) != 0) {
        return FV_JOURNAL_SEQUENCE_ERROR;
    }

    if (erased_offset == SIZE_MAX) {
        const size_t latest_block = latest_offset / journal->flash->erase_block_size;
        size_t erase_block = latest_block == 0u ? 1u : 0u;
        if (!journal->flash->ops->erase(
                journal->flash,
                erase_block * journal->flash->erase_block_size,
                journal->flash->erase_block_size)) {
            return FV_JOURNAL_IO_ERROR;
        }
        erased_offset = erase_block * journal->flash->erase_block_size;
    }

    uint8_t record[FV_JOURNAL_RECORD_SIZE];
    if (!encode_record(journal, state, record)) return FV_JOURNAL_IO_ERROR;
    return journal->flash->ops->program(journal->flash, erased_offset, record,
                                        sizeof(record))
        ? FV_JOURNAL_OK
        : FV_JOURNAL_IO_ERROR;
}
