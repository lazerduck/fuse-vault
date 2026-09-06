#include "fuse_vault/device_roots.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OTP_ROWS 4096u
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

typedef struct {
    uint16_t rows[OTP_ROWS];
    size_t writes_before_failure;
    bool failure_enabled;
} mock_otp_t;

static bool mock_read(fv_device_roots_storage_t *storage, uint16_t first_row,
                      uint16_t *output, size_t count) {
    mock_otp_t *otp = storage->context;
    if ((size_t)first_row + count > OTP_ROWS) return false;
    memcpy(output, otp->rows + first_row, count * sizeof(uint16_t));
    return true;
}

static bool mock_write(fv_device_roots_storage_t *storage, uint16_t first_row,
                       const uint16_t *input, size_t count) {
    mock_otp_t *otp = storage->context;
    if ((size_t)first_row + count > OTP_ROWS) return false;
    for (size_t index = 0u; index < count; ++index) {
        if (otp->failure_enabled && otp->writes_before_failure == 0u) return false;
        if ((otp->rows[first_row + index] & input[index]) !=
            otp->rows[first_row + index]) return false;
        otp->rows[first_row + index] |= input[index];
        if (otp->failure_enabled) --otp->writes_before_failure;
    }
    return true;
}

static const fv_device_roots_storage_ops_t MOCK_OPS = {
    .read_ecc_rows = mock_read,
    .write_ecc_rows = mock_write,
};

static void fixture(fv_device_roots_storage_t *storage, mock_otp_t *otp,
                    bool provisioning) {
    memset(otp, 0, sizeof(*otp));
    *storage = (fv_device_roots_storage_t) {
        .ops = &MOCK_OPS,
        .context = otp,
        .root_programming_enabled = provisioning,
    };
}

static fv_device_secret_t sample_roots(void) {
    fv_device_secret_t roots;
    for (size_t index = 0u; index < sizeof(roots.device_secret); ++index) {
        roots.device_secret[index] = (uint8_t)(index + 1u);
    }
    return roots;
}

static void test_lifecycle(void) {
    fv_device_roots_storage_t storage;
    mock_otp_t otp;
    fixture(&storage, &otp, true);
    CHECK(fv_device_roots_status(&storage) == FV_DEVICE_ROOTS_EMPTY);
    const fv_device_secret_t roots = sample_roots();
    CHECK(fv_device_roots_provision(&storage, &roots) == FV_DEVICE_ROOTS_OK);
    CHECK(fv_device_roots_status(&storage) == FV_DEVICE_ROOTS_ACTIVE);
    fv_device_secret_t recovered;
    CHECK(fv_device_roots_read(&storage, &recovered) == FV_DEVICE_ROOTS_ACTIVE);
    CHECK(memcmp(&recovered, &roots, sizeof(roots)) == 0);
    CHECK(fv_device_roots_provision(&storage, &roots) == FV_DEVICE_ROOTS_INVALID);
    CHECK(fv_device_roots_revoke(&storage) == FV_DEVICE_ROOTS_OK);
    CHECK(fv_device_roots_status(&storage) == FV_DEVICE_ROOTS_REVOKED);
    CHECK(fv_device_roots_provision(&storage, &roots) == FV_DEVICE_ROOTS_INVALID);
    CHECK(fv_device_roots_status(&storage) == FV_DEVICE_ROOTS_REVOKED);
    memset(&recovered, 0xa5, sizeof(recovered));
    CHECK(fv_device_roots_read(&storage, &recovered) == FV_DEVICE_ROOTS_REVOKED);
    const fv_device_secret_t zero = {0};
    CHECK(memcmp(&recovered, &zero, sizeof(zero)) == 0);
}

static void test_disabled_storage_cannot_provision(void) {
    fv_device_roots_storage_t storage;
    mock_otp_t otp;
    fixture(&storage, &otp, false);
    const fv_device_secret_t roots = sample_roots();
    CHECK(fv_device_roots_provision(&storage, &roots) ==
          FV_DEVICE_ROOTS_NOT_PERMITTED);
    CHECK(fv_device_roots_status(&storage) == FV_DEVICE_ROOTS_EMPTY);
}

static void test_reserved_rows_fail_closed(void) {
    fv_device_roots_storage_t storage;
    mock_otp_t otp;
    const fv_device_secret_t roots = sample_roots();

    fixture(&storage, &otp, true);
    otp.rows[FV_DEVICE_ROOTS_ACTIVE_ROW + 1u] = 1u;
    CHECK(fv_device_roots_status(&storage) == FV_DEVICE_ROOTS_INVALID);
    CHECK(fv_device_roots_provision(&storage, &roots) ==
          FV_DEVICE_ROOTS_INVALID);

    fixture(&storage, &otp, true);
    otp.rows[FV_DEVICE_ROOTS_REVOKED_ROW + 1u] = 1u;
    CHECK(fv_device_roots_status(&storage) == FV_DEVICE_ROOTS_INVALID);
    CHECK(fv_device_roots_provision(&storage, &roots) ==
          FV_DEVICE_ROOTS_INVALID);
}

static void test_every_interrupted_provision_fails_closed(void) {
    const fv_device_secret_t roots = sample_roots();
    for (size_t completed = 0u; completed < 34u; ++completed) {
        fv_device_roots_storage_t storage;
        mock_otp_t otp;
        fixture(&storage, &otp, true);
        otp.failure_enabled = true;
        otp.writes_before_failure = completed;
        CHECK(fv_device_roots_provision(&storage, &roots) ==
              FV_DEVICE_ROOTS_IO_ERROR);
        const fv_device_roots_result_t status = fv_device_roots_status(&storage);
        CHECK(status == (completed == 0u ? FV_DEVICE_ROOTS_EMPTY
                                        : FV_DEVICE_ROOTS_INVALID));
        if (completed != 0u) {
            otp.failure_enabled = false;
            CHECK(fv_device_roots_provision(&storage, &roots) ==
                  FV_DEVICE_ROOTS_INVALID);
        }
    }
}

int main(void) {
    test_lifecycle();
    test_disabled_storage_cannot_provision();
    test_reserved_rows_fail_closed();
    test_every_interrupted_provision_fails_closed();
    puts("All device-root lifecycle tests passed.");
    return EXIT_SUCCESS;
}
