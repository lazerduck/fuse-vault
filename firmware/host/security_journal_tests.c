#include "fuse_vault/security_journal.h"
#include "nor_flash.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)
#define TEST_FLASH_SIZE 8192u

typedef struct {
    uint32_t seed_a;
    uint32_t seed_b;
} test_auth_context_t;

static void check(bool condition, const char *expression, const char *file,
                  int line) {
    if (!condition) {
        fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
        exit(EXIT_FAILURE);
    }
}

static uint32_t test_hash(uint32_t seed, const uint8_t *data, size_t length) {
    uint32_t value = seed;
    for (size_t index = 0u; index < length; ++index) {
        value ^= data[index];
        value *= 16777619u;
        value ^= value >> 13u;
    }
    return value;
}

static void expand_test_tag(uint32_t seed, const uint8_t *record, size_t length,
                            uint8_t output[FV_JOURNAL_TAG_SIZE]) {
    uint32_t value = test_hash(seed, record, length);
    for (size_t index = 0u; index < FV_JOURNAL_TAG_SIZE; ++index) {
        value = value * 1664525u + 1013904223u;
        output[index] = (uint8_t)(value >> 24u);
    }
}

/* Test fixture only. This is intentionally not a cryptographic MAC. */
static bool compute_test_tags(
    fv_journal_authenticator_t *authenticator, const uint8_t *record,
    size_t length, uint8_t tag_a[FV_JOURNAL_TAG_SIZE],
    uint8_t tag_b[FV_JOURNAL_TAG_SIZE]) {
    const test_auth_context_t *context = authenticator->context;
    expand_test_tag(context->seed_a, record, length, tag_a);
    expand_test_tag(context->seed_b, record, length, tag_b);
    return true;
}

static bool flash_read(fv_journal_flash_t *flash, size_t offset,
                       uint8_t *output, size_t length) {
    return fv_nor_read(flash->context, offset, output, length) == FV_NOR_OK;
}

static bool flash_program(fv_journal_flash_t *flash, size_t offset,
                          const uint8_t *input, size_t length) {
    return fv_nor_program(flash->context, offset, input, length) == FV_NOR_OK;
}

static bool flash_erase(fv_journal_flash_t *flash, size_t offset,
                        size_t length) {
    return fv_nor_erase(flash->context, offset, length) == FV_NOR_OK;
}

static const fv_journal_flash_ops_t FLASH_OPS = {
    .read = flash_read,
    .program = flash_program,
    .erase = flash_erase,
};

static const fv_journal_authenticator_ops_t AUTH_OPS = {
    .compute_tags = compute_test_tags,
};

typedef struct {
    uint8_t storage[TEST_FLASH_SIZE];
    fv_nor_flash_t nor;
    fv_journal_flash_t flash;
    test_auth_context_t auth_context;
    fv_journal_authenticator_t authenticator;
    fv_security_journal_t journal;
} fixture_t;

static void fixture_init(fixture_t *fixture) {
    CHECK(fv_nor_init(&fixture->nor, fixture->storage, sizeof(fixture->storage),
                      4096u, FV_JOURNAL_RECORD_SIZE));
    fixture->flash = (fv_journal_flash_t) {
        .ops = &FLASH_OPS,
        .context = &fixture->nor,
        .size = sizeof(fixture->storage),
        .erase_block_size = 4096u,
        .program_size = FV_JOURNAL_RECORD_SIZE,
    };
    fixture->auth_context = (test_auth_context_t) {
        .seed_a = 0x12345678u,
        .seed_b = 0x9abcdef0u,
    };
    fixture->authenticator = (fv_journal_authenticator_t) {
        .ops = &AUTH_OPS,
        .context = &fixture->auth_context,
    };
    CHECK(fv_security_journal_init(&fixture->journal, &fixture->flash,
                                   &fixture->authenticator));
}

static fv_journal_state_t initial_state(void) {
    fv_journal_state_t state = {
        .sequence = 1u,
        .previous_sequence = 0u,
        .failed_attempts = 0u,
        .provisioned = true,
    };
    for (size_t index = 0u; index < FV_VAULT_ID_SIZE; ++index) {
        state.vault_id[index] = (uint8_t)(index + 1u);
    }
    return state;
}

static void test_append_recover_and_sequence(void) {
    fixture_t fixture;
    fixture_init(&fixture);
    fv_journal_state_t recovered;
    CHECK(fv_security_journal_recover(&fixture.journal, &recovered) ==
          FV_JOURNAL_EMPTY);

    fv_journal_state_t state = initial_state();
    CHECK(fv_security_journal_append(&fixture.journal, &state) == FV_JOURNAL_OK);
    CHECK(fv_security_journal_recover(&fixture.journal, &recovered) ==
          FV_JOURNAL_OK);
    CHECK(recovered.sequence == 1u);
    CHECK(recovered.failed_attempts == 0u);

    state.sequence = 3u;
    state.previous_sequence = 1u;
    CHECK(fv_security_journal_append(&fixture.journal, &state) ==
          FV_JOURNAL_SEQUENCE_ERROR);
}

static void test_every_torn_program_recovers_latest_complete_state(void) {
    for (size_t cut = 0u; cut < FV_JOURNAL_RECORD_SIZE; ++cut) {
        fixture_t fixture;
        fixture_init(&fixture);
        fv_journal_state_t state = initial_state();
        CHECK(fv_security_journal_append(&fixture.journal, &state) ==
              FV_JOURNAL_OK);

        state.previous_sequence = state.sequence;
        ++state.sequence;
        state.failed_attempts = 1u;
        fv_nor_fail_after(&fixture.nor, cut);
        CHECK(fv_security_journal_append(&fixture.journal, &state) ==
              FV_JOURNAL_IO_ERROR);

        fv_journal_state_t recovered;
        CHECK(fv_security_journal_recover(&fixture.journal, &recovered) ==
              FV_JOURNAL_OK);
        const bool new_record_is_complete = cut >= FV_JOURNAL_COMMITTED_SIZE;
        CHECK(recovered.sequence == (new_record_is_complete ? 2u : 1u));
        CHECK(recovered.failed_attempts == (new_record_is_complete ? 1u : 0u));
    }
}

static void test_corrupt_latest_falls_back(void) {
    fixture_t fixture;
    fixture_init(&fixture);
    fv_journal_state_t state = initial_state();
    CHECK(fv_security_journal_append(&fixture.journal, &state) == FV_JOURNAL_OK);
    state.previous_sequence = state.sequence;
    ++state.sequence;
    state.failed_attempts = 1u;
    CHECK(fv_security_journal_append(&fixture.journal, &state) == FV_JOURNAL_OK);

    bool changed = false;
    for (size_t index = FV_JOURNAL_AUTHENTICATED_SIZE;
         index < FV_JOURNAL_AUTHENTICATED_SIZE + FV_JOURNAL_TAG_SIZE; ++index) {
        uint8_t *byte = &fixture.storage[FV_JOURNAL_RECORD_SIZE + index];
        if (*byte != 0u) {
            *byte &= (uint8_t)(*byte - 1u);
            changed = true;
            break;
        }
    }
    CHECK(changed);
    fv_journal_state_t recovered;
    CHECK(fv_security_journal_recover(&fixture.journal, &recovered) ==
          FV_JOURNAL_OK);
    CHECK(recovered.sequence == 1u);
}

static void test_sector_rotation_retains_latest(void) {
    fixture_t fixture;
    fixture_init(&fixture);
    fv_journal_state_t state = initial_state();
    for (unsigned update = 0u; update < 40u; ++update) {
        CHECK(fv_security_journal_append(&fixture.journal, &state) ==
              FV_JOURNAL_OK);
        state.previous_sequence = state.sequence;
        ++state.sequence;
        state.failed_attempts = (uint8_t)(update % 10u);
    }
    fv_journal_state_t recovered;
    CHECK(fv_security_journal_recover(&fixture.journal, &recovered) ==
          FV_JOURNAL_OK);
    CHECK(recovered.sequence == 40u);
}

int main(void) {
    test_append_recover_and_sequence();
    test_every_torn_program_recovers_latest_complete_state();
    test_corrupt_latest_falls_back();
    test_sector_rotation_retains_latest();
    puts("All authenticated security journal tests passed.");
    return EXIT_SUCCESS;
}
