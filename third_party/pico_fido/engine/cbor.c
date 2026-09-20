/* Modified for Fuse Vault, 2026-09-06. See firmware/third_party/pico_fido/README.fuse-vault.md
 * for the platform port, feature restrictions and security/lifecycle changes. */
/*
 * This file is part of the Pico FIDO distribution (https://github.com/polhenarejos/pico-fido).
 * Copyright (c) 2022 Pol Henarejos.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "picokeys.h"
#if defined(PICO_PLATFORM)
#include "pico/stdlib.h"
#endif
#include "hid/ctap_hid.h"
#include "ctap.h"
#include "fido.h"
#include "usb.h"
#include "apdu.h"
#include "management.h"
#include "ctap2_cbor.h"
#include "version.h"

static CborError COSE_key_params(int crv, int alg, mbedtls_ecp_group *grp, mbedtls_ecp_point *Q, CborEncoder *mapEncoderParent, CborEncoder *mapEncoder) {
    CborError error = CborNoError;
    int kty = 1;
    if (crv == FIDO2_CURVE_P256 || crv == FIDO2_CURVE_P384 || crv == FIDO2_CURVE_P521 ||
        crv == FIDO2_CURVE_P256K1) {
        kty = 2;
    }

    CBOR_CHECK(cbor_encoder_create_map(mapEncoderParent, mapEncoder, kty == 2 ? 5 : 4));

    CBOR_CHECK(cbor_encode_uint(mapEncoder, 1));
    CBOR_CHECK(cbor_encode_uint(mapEncoder, kty));

    CBOR_CHECK(cbor_encode_uint(mapEncoder, 3));
    CBOR_CHECK(cbor_encode_negative_int(mapEncoder, -alg));

    CBOR_CHECK(cbor_encode_negative_int(mapEncoder, 1));
    CBOR_CHECK(cbor_encode_uint(mapEncoder, crv));


    CBOR_CHECK(cbor_encode_negative_int(mapEncoder, 2));
    uint8_t pkey[67];
    if (kty == 2) {
        size_t plen = mbedtls_mpi_size(&grp->P);
        CBOR_CHECK(mbedtls_mpi_write_binary(&Q->X, pkey, plen));
        CBOR_CHECK(cbor_encode_byte_string(mapEncoder, pkey, plen));

        CBOR_CHECK(cbor_encode_negative_int(mapEncoder, 3));

        CBOR_CHECK(mbedtls_mpi_write_binary(&Q->Y, pkey, plen));
        CBOR_CHECK(cbor_encode_byte_string(mapEncoder, pkey, plen));
    }
    else {
        size_t olen = 0;
        CBOR_CHECK(mbedtls_ecp_point_write_binary(grp, Q, MBEDTLS_ECP_PF_COMPRESSED, &olen, pkey,
                                                  sizeof(pkey)));
        CBOR_CHECK(cbor_encode_byte_string(mapEncoder, pkey, olen));
    }

    CBOR_CHECK(cbor_encoder_close_container(mapEncoderParent, mapEncoder));
err:
    return error;
}
CborError COSE_key(mbedtls_ecp_keypair *key, int alg, CborEncoder *mapEncoderParent, CborEncoder *mapEncoder) {
    int crv = mbedtls_curve_to_fido(key->grp.id);
    return COSE_key_params(crv, alg, &key->grp, &key->Q, mapEncoderParent, mapEncoder);
}

CborError COSE_cached_key(const uint8_t *data, size_t data_len, CborEncoder *mapEncoderParent, CborEncoder *mapEncoder) {
    if (!data || data_len == 0 || !mapEncoderParent || !mapEncoder) {
        return CborErrorIllegalType;
    }
    CborParser parser;
    CborValue value;
    int64_t kty = 0;
    int64_t alg = 0;
    int64_t crv = 0;
    CborByteString x = { 0 };
    CborByteString y = { 0 };
    CborError error = cbor_parser_init(data, data_len, 0, &parser, &value);
    if (error == CborNoError) {
        error = COSE_read_key(&value, &kty, &alg, &crv, &x, &y);
    }
    if (error != CborNoError || !x.present || x.len == 0) {
        CBOR_FREE_BYTE_STRING(x);
        CBOR_FREE_BYTE_STRING(y);
        return error == CborNoError ? CborErrorImproperValue : error;
    }
    CBOR_CHECK(cbor_encoder_create_map(mapEncoderParent, mapEncoder, y.present ? 5 : 4));
    CBOR_CHECK(cbor_encode_int(mapEncoder, 1));
    CBOR_CHECK(cbor_encode_int(mapEncoder, kty));
    CBOR_CHECK(cbor_encode_int(mapEncoder, 3));
    CBOR_CHECK(cbor_encode_int(mapEncoder, alg));
    CBOR_CHECK(cbor_encode_int(mapEncoder, -1));
    CBOR_CHECK(cbor_encode_int(mapEncoder, crv));
    CBOR_CHECK(cbor_encode_int(mapEncoder, -2));
    CBOR_CHECK(cbor_encode_byte_string(mapEncoder, x.data, x.len));
    if (y.present) {
        CBOR_CHECK(cbor_encode_int(mapEncoder, -3));
        CBOR_CHECK(cbor_encode_byte_string(mapEncoder, y.data, y.len));
    }
    CBOR_CHECK(cbor_encoder_close_container(mapEncoderParent, mapEncoder));
err:
    CBOR_FREE_BYTE_STRING(x);
    CBOR_FREE_BYTE_STRING(y);
    return error;
}
CborError COSE_key_shared(mbedtls_ecdh_context *key,
                          CborEncoder *mapEncoderParent,
                          CborEncoder *mapEncoder) {
    int crv = mbedtls_curve_to_fido(key->ctx.mbed_ecdh.grp.id), alg = FIDO2_ALG_ECDH_ES_HKDF_256;
    return COSE_key_params(crv, alg, &key->ctx.mbed_ecdh.grp, &key->ctx.mbed_ecdh.Q, mapEncoderParent, mapEncoder);
}
CborError COSE_public_key(int alg, CborEncoder *mapEncoderParent, CborEncoder *mapEncoder) {
    CborError error = CborNoError;
    CBOR_CHECK(cbor_encoder_create_map(mapEncoderParent, mapEncoder, 2));
    CBOR_CHECK(cbor_encode_text_stringz(mapEncoder, "alg"));
    CBOR_CHECK(cbor_encode_negative_int(mapEncoder, -alg));
    CBOR_CHECK(cbor_encode_text_stringz(mapEncoder, "type"));
    CBOR_CHECK(cbor_encode_text_stringz(mapEncoder, "public-key"));
    CBOR_CHECK(cbor_encoder_close_container(mapEncoderParent, mapEncoder));
err:
    return error;
}
CborError COSE_read_key(CborValue *f, int64_t *kty, int64_t *alg, int64_t *crv, CborByteString *kax, CborByteString *kay) {
    int64_t kkey = 0;
    CborError error = CborNoError;
    CBOR_PARSE_MAP_START(*f, 0)
    {
        CBOR_FIELD_GET_INT(kkey, 0);
        if (kkey == 1) {
            CBOR_FIELD_GET_INT(*kty, 0);
        }
        else if (kkey == 3) {
            CBOR_FIELD_GET_INT(*alg, 0);
        }
        else if (kkey == -1) {
            CBOR_FIELD_GET_INT(*crv, 0);
        }
        else if (kkey == -2) {
            CBOR_FIELD_GET_BYTES(*kax, 0);
        }
        else if (kkey == -3) {
            CBOR_FIELD_GET_BYTES(*kay, 0);
        }
        else {
            CBOR_ADVANCE(0);
        }
    }
    CBOR_PARSE_MAP_END(*f, 0);
err:
    return error;
}
