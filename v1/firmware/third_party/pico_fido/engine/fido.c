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
#include "button.h"
#include "fido.h"
#include "led/led.h"
#include "serial.h"
#include "apdu.h"
#include "ctap.h"
#include "files.h"
#include "usb.h"
#include "random.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/sha256.h"
#if defined(USB_ITF_CCID)
#include "ccid/ccid.h"
#endif
#if defined(PICO_PLATFORM)
#include "bsp/board.h"
#endif
#include <math.h>
#include "management.h"
#include "object_authorization.h"
#include "hid/ctap_hid.h"
#include "ctap2_cbor.h"
#include "credential.h"
#include "version.h"
#include "crypto_utils.h"
#include "otp.h"


pinUvAuthToken_t paut = { 0 };
persistentPinUvAuthToken_t ppaut = { 0 };

uint8_t keydev_dec[32];
bool has_keydev_dec = false;
bool keydev_unlocked = false;
uint8_t session_pin[32] = { 0 };

uint8_t certdev_sha256[32] = { 0 };

mbedtls_ecp_group_id fido_curve_to_mbedtls(int curve) {
    if (curve == FIDO2_CURVE_P256) {
        return MBEDTLS_ECP_DP_SECP256R1;
    }
    else if (curve == FIDO2_CURVE_P384) {
        return MBEDTLS_ECP_DP_SECP384R1;
    }
    else if (curve == FIDO2_CURVE_P521) {
        return MBEDTLS_ECP_DP_SECP521R1;
    }
    else if (curve == FIDO2_CURVE_P256K1) {
        return MBEDTLS_ECP_DP_SECP256K1;
    }
    else if (curve == FIDO2_CURVE_X25519) {
        return MBEDTLS_ECP_DP_CURVE25519;
    }
    else if (curve == FIDO2_CURVE_X448) {
        return MBEDTLS_ECP_DP_CURVE448;
    }
#ifdef MBEDTLS_EDDSA_C
    else if (curve == FIDO2_CURVE_ED25519) {
        return MBEDTLS_ECP_DP_ED25519;
    }
    else if (curve == FIDO2_CURVE_ED448) {
        return MBEDTLS_ECP_DP_ED448;
    }
#endif
    else if (curve == FIDO2_CURVE_BP256R1) {
        return MBEDTLS_ECP_DP_BP256R1;
    }
    else if (curve == FIDO2_CURVE_BP384R1) {
        return MBEDTLS_ECP_DP_BP384R1;
    }
    else if (curve == FIDO2_CURVE_BP512R1) {
        return MBEDTLS_ECP_DP_BP512R1;
    }
    return MBEDTLS_ECP_DP_NONE;
}
int mbedtls_curve_to_fido(mbedtls_ecp_group_id id) {
    if (id == MBEDTLS_ECP_DP_SECP256R1) {
        return FIDO2_CURVE_P256;
    }
    else if (id == MBEDTLS_ECP_DP_SECP384R1) {
        return FIDO2_CURVE_P384;
    }
    else if (id == MBEDTLS_ECP_DP_SECP521R1) {
        return FIDO2_CURVE_P521;
    }
    else if (id == MBEDTLS_ECP_DP_SECP256K1) {
        return FIDO2_CURVE_P256K1;
    }
    else if (id == MBEDTLS_ECP_DP_CURVE25519) {
        return FIDO2_CURVE_X25519;
    }
    else if (id == MBEDTLS_ECP_DP_CURVE448) {
        return FIDO2_CURVE_X448;
    }
#ifdef MBEDTLS_EDDSA_C
    else if (id == MBEDTLS_ECP_DP_ED25519) {
        return FIDO2_CURVE_ED25519;
    }
    else if (id == MBEDTLS_ECP_DP_ED448) {
        return FIDO2_CURVE_ED448;
    }
#endif
    return 0;
}

int fido_load_key(int curve, const uint8_t *cred_id, mbedtls_ecp_keypair *key) {
    mbedtls_ecp_group_id mbedtls_curve = fido_curve_to_mbedtls(curve);
    if (mbedtls_curve == MBEDTLS_ECP_DP_NONE) {
        return CTAP2_ERR_UNSUPPORTED_ALGORITHM;
    }
    uint8_t key_path[KEY_PATH_LEN];
    memcpy(key_path, cred_id, KEY_PATH_LEN);
    uint32_t key_path_first = 0x80000000u | 10022u;
    memcpy(key_path, &key_path_first, sizeof(key_path_first));
    for (size_t i = 1; i < KEY_PATH_ENTRIES; i++) {
        uint32_t part = 0;
        memcpy(&part, key_path + i * sizeof(uint32_t), sizeof(part));
        part |= 0x80000000u;
        memcpy(key_path + i * sizeof(uint32_t), &part, sizeof(part));
    }
    return derive_key(NULL, false, key_path, mbedtls_curve, key);
}

int load_keydev(uint8_t key[32]) {
    bool pin_wrapped = false;

    if (has_keydev_dec == false && !file_has_data(ef_keydev)) {
        return PICOKEYS_ERR_MEMORY_FATAL;
    }

    if (has_keydev_dec == true) {
        memcpy(key, keydev_dec, sizeof(keydev_dec));
    }
    else {
        uint32_t fid_size = file_get_size(ef_keydev);
        if (fid_size == 32) {
            memcpy(key, file_get_data(ef_keydev), 32);
            if (otp_key_1 && aes_decrypt(CONST_BYTE_ARRAY(otp_key_1, 32), NULL, PICOKEYS_AES_MODE_CBC, BYTE_ARRAY(key, 32)) != PICOKEYS_OK) {
                return PICOKEYS_EXEC_ERROR;
            }
        }
        else if (fid_size == 33 || fid_size == 61) {
            uint8_t format = *file_get_data(ef_keydev);
            if (format == 0x01 || format == 0x02 || format == 0x03) { // Format indicator
                if (format == 0x02 || format == 0x03) {
                    pin_wrapped = true;
                    uint8_t tmp_key[61], version = format == 0x03 ? 2 : 1;
                    memcpy(tmp_key, file_get_data(ef_keydev), sizeof(tmp_key));
                    int ret = decrypt_with_aad(session_pin, CONST_BYTE_ARRAY(tmp_key + 1, 60), version, key);
                    if (ret != PICOKEYS_OK) {
                        return PICOKEYS_EXEC_ERROR;
                    }
                    if (format == 0x02) {
                        tmp_key[0] = 0x03;
                        ret = encrypt_with_aad(session_pin, CONST_BYTE_ARRAY(key, 32), 2, tmp_key + 1);
                        if (ret != PICOKEYS_OK) {
                            mbedtls_platform_zeroize(tmp_key, sizeof(tmp_key));
                            return PICOKEYS_EXEC_ERROR;
                        }
                        file_put_data(ef_keydev, CONST_BYTE_ARRAY(tmp_key, sizeof(tmp_key)));
                        flash_commit();
                    }
                    mbedtls_platform_zeroize(tmp_key, sizeof(tmp_key));
                }
                else {
                    memcpy(key, file_get_data(ef_keydev) + 1, 32);
                }
                uint8_t kbase[32];
                derive_kbase(kbase);
                int ret = aes_decrypt(CONST_BYTE_ARRAY(kbase, 32), pico_serial_hash, PICOKEYS_AES_MODE_CBC, BYTE_ARRAY(key, 32));
                if (ret != PICOKEYS_OK) {
                    mbedtls_platform_zeroize(kbase, sizeof(kbase));
                    return PICOKEYS_EXEC_ERROR;
                }
                mbedtls_platform_zeroize(kbase, sizeof(kbase));
            }
        }
    }

    if (pin_wrapped) {
        keydev_unlocked = true;
    }
    return PICOKEYS_OK;
}

int verify_key(const uint8_t *appId, const uint8_t *keyHandle, mbedtls_ecp_keypair *key) {
    for (size_t i = 0; i < KEY_PATH_ENTRIES; i++) {
        uint32_t k = 0;
        memcpy(&k, &keyHandle[i * sizeof(uint32_t)], sizeof(k));
        if (!(k & 0x80000000)) {
            return -1;
        }
    }
    mbedtls_ecdsa_context ctx;
    if (key == NULL) {
        mbedtls_ecdsa_init(&ctx);
        key = &ctx;
        if (derive_key(appId, false, (uint8_t *) keyHandle, MBEDTLS_ECP_DP_SECP256R1, &ctx) != 0) {
            mbedtls_ecdsa_free(&ctx);
            return -3;
        }
    }
    uint8_t hmac[32], d[32];
    size_t olen = 0;
    int ret = mbedtls_ecp_write_key_ext(key, &olen, d, sizeof(d));
    if (key == &ctx) {
        mbedtls_ecdsa_free(&ctx);
    }
    if (ret != 0) {
        return -2;
    }
    uint8_t key_base[CTAP_APPID_SIZE + KEY_PATH_LEN];
    memcpy(key_base, appId, CTAP_APPID_SIZE);
    memcpy(key_base + CTAP_APPID_SIZE, keyHandle, KEY_PATH_LEN);
    ret = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), d, 32, key_base, sizeof(key_base), hmac);
    mbedtls_platform_zeroize(d, sizeof(d));
    return mbedtls_ct_memcmp(keyHandle + KEY_PATH_LEN, hmac, sizeof(hmac));
}

int derive_key(const uint8_t *app_id, bool new_key, uint8_t *key_handle, int curve, mbedtls_ecp_keypair *key) {
    uint8_t outk[67] = { 0 }; //SECP521R1 key is 66 bytes length
    int r = 0;
    memset(outk, 0, sizeof(outk));
    if ((r = load_keydev(outk)) != PICOKEYS_OK) {
        fv_pico_log("Error loading keydev: %d\n", r);
        return r;
    }
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
    for (size_t i = 0; i < KEY_PATH_ENTRIES; i++) {
        if (new_key == true) {
            uint32_t val = 0;
            random_fill_buffer(BYTE_ARRAY((uint8_t *)&val, sizeof(val)));
            val |= 0x80000000;
            memcpy(&key_handle[i * sizeof(uint32_t)], &val, sizeof(uint32_t));
        }
        r = mbedtls_hkdf(md_info, &key_handle[i * sizeof(uint32_t)], sizeof(uint32_t), outk, 32, outk + 32, 32, outk, sizeof(outk));
        if (r != 0) {
            mbedtls_platform_zeroize(outk, sizeof(outk));
            return r;
        }
    }
    if (new_key == true) {
        uint8_t key_base[CTAP_APPID_SIZE + KEY_PATH_LEN];
        memcpy(key_base, app_id, CTAP_APPID_SIZE);
        memcpy(key_base + CTAP_APPID_SIZE, key_handle, KEY_PATH_LEN);
        if ((r = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), outk, 32, key_base, sizeof(key_base), key_handle + 32)) != 0) {
            mbedtls_platform_zeroize(outk, sizeof(outk));
            return r;
        }
    }
    if (key != NULL) {
        mbedtls_ecp_group_load(&key->grp, curve);
        const mbedtls_ecp_curve_info *cinfo = mbedtls_ecp_curve_info_from_grp_id(curve);
        if (cinfo == NULL) {
            return 1;
        }
        if (cinfo->bit_size % 8 != 0) {
            outk[0] >>= 8 - (cinfo->bit_size % 8);
        }
        r = mbedtls_ecp_read_key(curve, key, outk, (size_t)((cinfo->bit_size + 7) / 8));
        mbedtls_platform_zeroize(outk, sizeof(outk));
        if (r != 0) {
            return r;
        }
        return mbedtls_ecp_keypair_calc_public(key, random_fill_iterator, NULL);
    }
    mbedtls_platform_zeroize(outk, sizeof(outk));
    return r;
}

int encrypt_keydev_f1(const uint8_t keydev[32]) {
    uint8_t kdata[33] = {0};
    kdata[0] = 0x01; // Format indicator
    memcpy(kdata + 1, keydev, 32);
    uint8_t kbase[32];
    derive_kbase(kbase);
    int ret = aes_encrypt(CONST_BYTE_ARRAY(kbase, 32), pico_serial_hash, PICOKEYS_AES_MODE_CBC, BYTE_ARRAY(kdata + 1, 32));
    mbedtls_platform_zeroize(kbase, sizeof(kbase));
    if (ret != PICOKEYS_OK) {
        return ret;
    }
    ret = file_put_data(ef_keydev, CONST_BYTE_ARRAY(kdata, 33));
    mbedtls_platform_zeroize(kdata, sizeof(kdata));
    flash_commit();
    return ret;
}

int scan_files_fido(void) {
    ef_keydev = file_search_by_fid(EF_KEY_DEV, NULL, SPECIFY_EF);
    ef_keydev_enc = file_search_by_fid(EF_KEY_DEV_ENC, NULL, SPECIFY_EF);
    ef_vault_key = file_search_by_fid(EF_VAULT_KEY, NULL, SPECIFY_EF);
    if (ef_keydev) {
        if (!file_has_data(ef_keydev) && !file_has_data(ef_keydev_enc)) {
            fv_pico_log("KEY DEVICE is empty. Generating SECP256R1 curve...");
            mbedtls_ecdsa_context ecdsa;
            mbedtls_ecdsa_init(&ecdsa);
            int ret = mbedtls_ecdsa_genkey(&ecdsa, MBEDTLS_ECP_DP_SECP256R1, random_fill_iterator, NULL);
            if (ret != 0) {
                mbedtls_ecdsa_free(&ecdsa);
                return ret;
            }
            uint8_t keydev[32] = {0};
            size_t key_size = 0;
            ret = mbedtls_ecp_write_key_ext(&ecdsa, &key_size, keydev, sizeof(keydev));
            if (ret != 0 || key_size != 32) {
                mbedtls_platform_zeroize(keydev, sizeof(keydev));
                mbedtls_ecdsa_free(&ecdsa);
                return ret != 0 ? ret : PICOKEYS_EXEC_ERROR;
            }
            ret = encrypt_keydev_f1(keydev);
            mbedtls_platform_zeroize(keydev, sizeof(keydev));
            mbedtls_ecdsa_free(&ecdsa);
            if (ret != PICOKEYS_OK) {
                return ret;
            }
            fv_pico_log(" done!\n");
        }
    }
    else {
        fv_pico_log("FATAL ERROR: KEY DEV not found in memory!\r\n");
    }
    ef_certdev = file_search_by_fid(EF_EE_DEV, NULL, SPECIFY_EF);
    /* Fuse Vault uses credential self-attestation only. */
    memset(certdev_sha256, 0, sizeof(certdev_sha256));
    ef_counter = file_search_by_fid(EF_COUNTER, NULL, SPECIFY_EF);
    if (ef_counter) {
        if (!file_has_data(ef_counter)) {
            uint32_t v = 0;
            file_put_data(ef_counter, CONST_BYTE_ARRAY((uint8_t *)&v, sizeof(v)));
        }
    }
    else {
        fv_pico_log("FATAL ERROR: Global counter not found in memory!\r\n");
    }
    ef_pin = file_search_by_fid(EF_PIN, NULL, SPECIFY_EF);
    ef_pin_admin = file_search_by_fid(EF_PIN_ADMIN, NULL, SPECIFY_EF);
    ef_authtoken = file_search_by_fid(EF_AUTHTOKEN, NULL, SPECIFY_EF);
    if (ef_authtoken) {
        if (!file_has_data(ef_authtoken)) {
            uint8_t t[32];
            random_fill_buffer(BYTE_ARRAY(t, sizeof(t)));
            file_put_data(ef_authtoken, CONST_BYTE_ARRAY(t, sizeof(t)));
        }
        paut.data = file_get_data(ef_authtoken);
        paut.len = file_get_size(ef_authtoken);
    }
    else {
        fv_pico_log("FATAL ERROR: Auth Token not found in memory!\r\n");
    }
    file_t *ef_pauthtoken = file_search_by_fid(EF_PAUTHTOKEN, NULL, SPECIFY_EF);
    if (ef_pauthtoken) {
        if (!file_has_data(ef_pauthtoken)) {
            uint8_t t[32];
            random_fill_buffer(BYTE_ARRAY(t, sizeof(t)));
            file_put_data(ef_pauthtoken, CONST_BYTE_ARRAY(t, sizeof(t)));
        }
        ppaut.data = file_get_data(ef_pauthtoken);
        ppaut.len = file_get_size(ef_pauthtoken);
    }
    else {
        fv_pico_log("FATAL ERROR: Persistent Auth Token not found in memory!\r\n");
    }
    ef_largeblob = file_search_by_fid(EF_LARGEBLOB, NULL, SPECIFY_EF);
    if (!file_has_data(ef_largeblob)) {
        file_put_data(ef_largeblob, CONST_BYTE_ARRAY((const uint8_t *)"\x80\x76\xbe\x8b\x52\x8d\x00\x75\xf7\xaa\xe9\x8d\x6f\xa5\x7a\x6d\x3c", 17));
    }
    file_t *ef_dev_state = file_search_by_fid(EF_DEV_STATE, NULL, SPECIFY_EF);
    if (!file_has_data(ef_dev_state)) {
        file_put_data(ef_dev_state, CONST_BYTE_ARRAY(random_bytes_get(32), 32));
    }

    flash_commit();
    return PICOKEYS_OK;
}

void fv_pico_engine_fail(void);
void scan_all(void) {
    file_scan_flash();
    if (scan_files_fido() != PICOKEYS_OK) fv_pico_engine_fail();
}

extern bool needs_power_cycle;
void init_fido(void) {
    memset(&paut, 0, sizeof(paut));
    memset(&ppaut, 0, sizeof(ppaut));
    mbedtls_platform_zeroize(keydev_dec, sizeof(keydev_dec));
    mbedtls_platform_zeroize(session_pin, sizeof(session_pin));
    has_keydev_dec = false;
    fido_object_authorization_session_invalidate();
    keydev_unlocked = false;
    scan_all();
    if (credential_migrate_rp_secure() != PICOKEYS_OK) fv_pico_engine_fail();
#ifdef ENABLE_OTP_APP
    init_otp();
#endif
    needs_power_cycle = false;
}

int fv_pico_wait_presence(void);
int wait_button_pressed(void) { return fv_pico_wait_presence(); }

uint32_t user_present_time_limit = 0;

bool check_user_presence(void) {
    if (user_present_time_limit == 0 || user_present_time_limit + TRANSPORT_TIME_LIMIT < board_millis()) {
        bool previous_force_button_wait = force_button_wait;
#ifdef FORCE_BUTTON_WAIT
        force_button_wait = true;
#endif
        int ret = wait_button_pressed();
        force_button_wait = previous_force_button_wait;
        if (ret > 0) {
            return false;
        }
        //user_present_time_limit = board_millis();
    }
    return true;
}

void fido_led_3_blinks(void) {
#ifndef ENABLE_EMULATION
    led_blink_n_times(3, LED_COLOR_GREEN, 100, 100);
#endif
}

uint32_t get_sign_counter(void) {
    uint8_t *caddr = file_get_data(ef_counter);
    return get_uint32_le(caddr);
}

uint8_t get_opts(void) {
    file_t *ef = file_search_by_fid(EF_OPTS, NULL, SPECIFY_EF);
    if (file_has_data(ef)) {
        return *file_get_data(ef);
    }
    return 0;
}

void set_opts(uint8_t opts) {
    file_t *ef = file_search_by_fid(EF_OPTS, NULL, SPECIFY_EF);
    file_put_data(ef, CONST_BYTE_ARRAY(&opts, sizeof(uint8_t)));
    flash_commit();
}

int dev_state_update(dev_state_t state) {
    file_t *ef_dev_state = file_search_by_fid(EF_DEV_STATE, NULL, SPECIFY_EF);
    if (!ef_dev_state) {
        return PICOKEYS_ERR_FILE_NOT_FOUND;
    }
    if (file_get_size(ef_dev_state) == 32) {
        uint8_t dev_state[32] = {0};
        memcpy(dev_state, file_get_data(ef_dev_state), 32);
        if (state & DEV_STATE_DEV_ID) {
            random_fill_buffer(BYTE_ARRAY(dev_state, 16));
        }
        else if (state & DEV_STATE_CRED_STATE) {
            random_fill_buffer(BYTE_ARRAY(dev_state + 16, 16));
        }
        file_put_data(ef_dev_state, CONST_BYTE_ARRAY(dev_state, 32));
    }
    else {
        file_put_data(ef_dev_state, CONST_BYTE_ARRAY(random_bytes_get(32), 32));
    }
    flash_commit();
    return PICOKEYS_OK;
}

