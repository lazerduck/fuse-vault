/* Derived from Pico FIDO src/fido/cbor_get_info.c.
 * Copyright (c) 2022 Pol Henarejos.
 * SPDX-License-Identifier: AGPL-3.0-only
 * Modified for Fuse Vault, 2026-09-06: bounded discovery-only encoder; removed
 * storage, OTP, crypto, global APDU state and unsupported capability claims.
 * See LICENSE and README.fuse-vault.md. Distributed without any warranty.
 */
#include <cbor.h>
#include <stddef.h>
#include <stdint.h>

#define CHECK(call) do { if ((call) != CborNoError) return 0u; } while (0)

size_t fv_pico_fido_get_info(uint8_t *output, size_t capacity) {
    CborEncoder encoder, map, array, options;
    /* Development identity only; never borrow upstream attestation identity. */
    static const uint8_t aaguid[16] = {0};
    if (output == NULL) return 0u;
    cbor_encoder_init(&encoder, output, capacity, 0);
    CHECK(cbor_encoder_create_map(&encoder, &map, 4));
    CHECK(cbor_encode_uint(&map, 0x01));
    CHECK(cbor_encoder_create_array(&map, &array, 1));
    /* Protocol discovery for an explicitly gated, non-conformant prototype. */
    CHECK(cbor_encode_text_stringz(&array, "FIDO_2_0"));
    CHECK(cbor_encoder_close_container(&map, &array));
    CHECK(cbor_encode_uint(&map, 0x03));
    CHECK(cbor_encode_byte_string(&map, aaguid, sizeof(aaguid)));
    CHECK(cbor_encode_uint(&map, 0x04));
    CHECK(cbor_encoder_create_map(&map, &options, 3));
    CHECK(cbor_encode_text_stringz(&options, "rk"));
    CHECK(cbor_encode_boolean(&options, false));
    CHECK(cbor_encode_text_stringz(&options, "up"));
    CHECK(cbor_encode_boolean(&options, false));
    CHECK(cbor_encode_text_stringz(&options, "plat"));
    CHECK(cbor_encode_boolean(&options, false));
    CHECK(cbor_encoder_close_container(&map, &options));
    CHECK(cbor_encode_uint(&map, 0x05));
    CHECK(cbor_encode_uint(&map, 1024));
    CHECK(cbor_encoder_close_container(&encoder, &map));
    return cbor_encoder_get_buffer_size(&encoder, output);
}
