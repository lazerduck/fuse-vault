/* Fuse Vault platform adapter for the pinned Pico FIDO engine.
 * SPDX-License-Identifier: AGPL-3.0-only
 * No upstream hardware, boot, OTP or USB implementation is linked here. */
#include "fuse_vault/fido_engine.h"
#include "picokeys.h"
#include "fido.h"
#include "ctap.h"
#include "ctap2_cbor.h"
extern uint8_t keydev_dec[32];
extern bool has_keydev_dec;
#include "files.h"
#include "object_authorization.h"
#include "apdu.h"
#include "hid/ctap_hid.h"
#include "random.h"
#include "serial.h"
#include "otp.h"
#include "management.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"
#include <stdarg.h>
#include <time.h>
bool is_nk=false;
bool has_set_rtc(void) { return false; }
time_t get_rtc_time(void) { return 0; }
const known_app_t *find_app_by_rp_id_hash(const uint8_t *hash) { (void)hash; return NULL; }

static fv_fido_engine_ops_t platform;
static uint8_t *image;
static bool failed, dirty, opened, staging;
static uint32_t session_started;
void fv_pico_engine_fail(void) { failed = true; }
static uint8_t root_key[32], random_buffer[1024];
static uint8_t response_buffer[USB_BUFFER_SIZE];
static CTAPHID_FRAME request_frame;
CTAPHID_FRAME *ctap_req = &request_frame, *ctap_resp = (CTAPHID_FRAME *)response_buffer;
struct apdu apdu;
const bool _btrue = true, _bfalse = false;
/* Development identity; no upstream certificate/identity reuse. */
const uint8_t aaguid[16] = {'F','u','s','e','V','a','u','l','t',0,0,0,0,0,0,1};
const uint8_t *otp_key_1, *otp_key_2;
picokey_serial_t pico_serial;
char pico_serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
uint8_t pico_serial_hash[32];
phy_data_t phy_data = {.up_btn = 1, .enabled_curves = PHY_CURVE_SECP256R1};
bool force_button_wait = true;
uint8_t ITF_HID, ITF_HID_CTAP;
uint32_t board_millis(void) { return platform.millis ? platform.millis(platform.context) : 0; }
int fv_pico_log(const char *fmt, ...) { (void)fmt; return 0; }
bool cap_supported(uint16_t cap) { return cap == CAP_FIDO2; }
void led_blink_n_times(uint8_t n, uint8_t c, uint32_t a, uint32_t b) {
    (void)n; (void)c; (void)a; (void)b;
}
void led_set_mode(uint32_t mode) { (void)mode; }
bool fv_pico_reset_allowed(void) { return (uint32_t)(board_millis() - session_started) < 10000u; }
bool fv_pico_has_uv(void) { return platform.verify_user != NULL; }
uint8_t fv_pico_uv_retries(void) { return platform.uv_retries ? platform.uv_retries(platform.context) : 0; }
bool fv_pico_verify_user(const uint8_t *rp_hash) {
    return !failed && platform.verify_user && platform.verify_user(platform.context, rp_hash);
}
int fv_pico_wait_presence(void) {
    return !failed && platform.presence ? platform.presence(platform.context) : 2;
}
int random_fill_buffer(byte_array_t b) {
    if (!platform.random || !platform.random(platform.context,b.data,b.len)) {
        failed = true; if (b.data) memset(b.data,0,b.len); return PICOKEYS_EXEC_ERROR;
    }
    return 0;
}
int random_fill_iterator(void *ctx, unsigned char *out, size_t n) {
    (void)ctx; return random_fill_buffer(BYTE_ARRAY(out,n));
}
const uint8_t *random_bytes_get(size_t n) {
    if (n > sizeof(random_buffer)) { failed = true; return NULL; }
    if (random_fill_buffer(BYTE_ARRAY(random_buffer,n))) return NULL;
    return random_buffer;
}
/* Stable integer file offsets, never memory-mapped device flash addresses. */
#define STORE_BASE 4096u
static uint8_t *address(uintptr_t a, size_t n) {
    if (!image || a < STORE_BASE || n > FV_FIDO_STORE_BYTES ||
        a - STORE_BASE > FV_FIDO_STORE_BYTES - n) { failed = true; return NULL; }
    return image + a - STORE_BASE;
}
int flash_program_block(uintptr_t a, const_byte_array_t b) {
    uint8_t *p = address(a,b.len);
    if (!p || (!b.data && b.len)) return PICOKEYS_EXEC_ERROR;
    if (b.len) memmove(p,b.data,b.len); dirty = true; return 0;
}
int flash_program_halfword(uintptr_t a,uint16_t v) { return flash_program_block(a,CONST_BYTE_ARRAY((uint8_t*)&v,2)); }
int flash_program_word(uintptr_t a,uint32_t v) { return flash_program_block(a,CONST_BYTE_ARRAY((uint8_t*)&v,4)); }
int flash_program_uintptr(uintptr_t a,uintptr_t v) {
    if (v > UINT32_MAX) { failed=true; return PICOKEYS_EXEC_ERROR; }
    return flash_program_word(a,(uint32_t)v);
}
uint8_t *flash_read(uintptr_t a) {
    static uint8_t invalid[4096];
    uint8_t *p=address(a,1); return p ? p : invalid;
}
int flash_read_block(uintptr_t a,byte_array_t b) {
    uint8_t *p=address(a,b.len); if (!p) return PICOKEYS_EXEC_ERROR;
    memcpy(b.data,p,b.len); return 0;
}
uint8_t flash_read_uint8(uintptr_t a) { uint8_t v=0; (void)flash_read_block(a,BYTE_ARRAY(&v,1)); return v; }
uint16_t flash_read_uint16(uintptr_t a) { uint16_t v=0; (void)flash_read_block(a,BYTE_ARRAY((uint8_t*)&v,2)); return v; }
uint32_t flash_read_uint32(uintptr_t a) { uint32_t v=0; (void)flash_read_block(a,BYTE_ARRAY((uint8_t*)&v,4)); return v; }
uintptr_t flash_read_uintptr(uintptr_t a) { return flash_read_uint32(a); }
int flash_erase_page(uintptr_t a,size_t n) {
    uint8_t *p=address(a,n); if (!p) return PICOKEYS_EXEC_ERROR;
    memset(p,0xff,n); dirty=true; return 0;
}
bool flash_check_blank(const_byte_array_t b) {
    for (size_t i=0;i<b.len;i++) if (b.data[i]!=0xff) return false; return true;
}
void low_flash_task(void) {}
bool low_flash_commit_sync(uint32_t timeout) {
    (void)timeout;
    if (failed) return false;
    if (staging) return true;
    if (dirty) {
        if (!platform.commit || !platform.commit(platform.context,image,FV_FIDO_STORE_BYTES)) { failed=true; return false; }
        dirty=false;
    }
    return true;
}
void low_flash_commit(void) { (void)low_flash_commit_sync(0); }
void fv_pico_pin_session_clear(void);
void fv_pico_cred_session_clear(void);
void fv_pico_object_session_clear(void);
void fv_pico_credential_index_clear(void);
extern char *rp_id, *user_name, *display_name;
void fv_fido_engine_close(void) {
    fv_pico_pin_session_clear();
    fv_pico_cred_session_clear();
    fv_pico_object_session_clear();
    fv_pico_credential_index_clear();
    rp_id = user_name = display_name = NULL;
    fido_object_authorization_session_invalidate();
    reset_gna_state();
    memset(&paut,0,sizeof(paut)); memset(&ppaut,0,sizeof(ppaut));
    mbedtls_platform_zeroize(keydev_dec,sizeof(keydev_dec));
    mbedtls_platform_zeroize(session_pin,sizeof(session_pin));
    has_keydev_dec=false; keydev_unlocked=false;
    mbedtls_platform_zeroize(root_key,sizeof(root_key));
    mbedtls_platform_zeroize(random_buffer,sizeof(random_buffer));
    mbedtls_platform_zeroize(response_buffer,sizeof(response_buffer));
    if (image) mbedtls_platform_zeroize(image,FV_FIDO_STORE_BYTES);
    image=NULL; otp_key_1=otp_key_2=NULL; opened=false;
    memset(&platform,0,sizeof(platform));
}
bool fv_fido_engine_open(uint8_t store[FV_FIDO_STORE_BYTES], const uint8_t root[32],
                        const uint8_t device_id[16], const fv_fido_engine_ops_t *ops) {
    if (opened || !store || !root || !device_id || !ops || !ops->random ||
        !ops->commit || !ops->presence || !ops->millis) return false;
    platform=*ops; image=store; failed=false; dirty=false;
    session_started = board_millis();
    memcpy(root_key,root,32); otp_key_1=root_key; otp_key_2=NULL;
    memcpy(pico_serial.id,device_id,sizeof(pico_serial.id));
    mbedtls_sha256(device_id,16,pico_serial_hash,0);
    flash_set_bounds(STORE_BASE,STORE_BASE+FV_FIDO_STORE_BYTES);
    staging=true; init_fido(); staging=false;
    if (!low_flash_commit_sync(0)) failed=true;
    if (failed) { fv_fido_engine_close(); return false; }
    opened=true; return true;
}
static int engine_get_info(void) {
    CborEncoder e,m,a,o,k;
    CborError error=CborNoError;
    cbor_encoder_init(&e,apdu.rdata,USB_BUFFER_SIZE-8,0);
    CBOR_CHECK(cbor_encoder_create_map(&e,&m,8));
    CBOR_CHECK(cbor_encode_uint(&m,1)); CBOR_CHECK(cbor_encoder_create_array(&m,&a,2));
    CBOR_CHECK(cbor_encode_text_stringz(&a,"FIDO_2_0"));
    CBOR_CHECK(cbor_encode_text_stringz(&a,"FIDO_2_1")); CBOR_CHECK(cbor_encoder_close_container(&m,&a));
    CBOR_CHECK(cbor_encode_uint(&m,2)); CBOR_CHECK(cbor_encoder_create_array(&m,&a,0));
    CBOR_CHECK(cbor_encoder_close_container(&m,&a));
    CBOR_CHECK(cbor_encode_uint(&m,3)); CBOR_CHECK(cbor_encode_byte_string(&m,aaguid,16));
    CBOR_CHECK(cbor_encode_uint(&m,4)); CBOR_CHECK(cbor_encoder_create_map(&m,&o,fv_pico_has_uv()?6:5));
    CBOR_CHECK(cbor_encode_text_stringz(&o,"rk")); CBOR_CHECK(cbor_encode_boolean(&o,true));
    CBOR_CHECK(cbor_encode_text_stringz(&o,"up")); CBOR_CHECK(cbor_encode_boolean(&o,true));
    if (fv_pico_has_uv()) {
        CBOR_CHECK(cbor_encode_text_stringz(&o,"uv")); CBOR_CHECK(cbor_encode_boolean(&o,true));
        CBOR_CHECK(cbor_encode_text_stringz(&o,"alwaysUv")); CBOR_CHECK(cbor_encode_boolean(&o,true));
    }
    CBOR_CHECK(cbor_encode_text_stringz(&o,"credMgmt")); CBOR_CHECK(cbor_encode_boolean(&o,true));
    if (!fv_pico_has_uv()) {
        CBOR_CHECK(cbor_encode_text_stringz(&o,"clientPin")); CBOR_CHECK(cbor_encode_boolean(&o,file_has_data(ef_pin)));
    }
    CBOR_CHECK(cbor_encode_text_stringz(&o,"pinUvAuthToken")); CBOR_CHECK(cbor_encode_boolean(&o,true));
    CBOR_CHECK(cbor_encoder_close_container(&m,&o));
    CBOR_CHECK(cbor_encode_uint(&m,5)); CBOR_CHECK(cbor_encode_uint(&m,FV_FIDO_ENGINE_MESSAGE_SIZE));
    CBOR_CHECK(cbor_encode_uint(&m,6)); CBOR_CHECK(cbor_encoder_create_array(&m,&a,2));
    CBOR_CHECK(cbor_encode_uint(&a,1)); CBOR_CHECK(cbor_encode_uint(&a,2)); CBOR_CHECK(cbor_encoder_close_container(&m,&a));
    CBOR_CHECK(cbor_encode_uint(&m,9)); CBOR_CHECK(cbor_encoder_create_array(&m,&a,1));
    CBOR_CHECK(cbor_encode_text_stringz(&a,"usb")); CBOR_CHECK(cbor_encoder_close_container(&m,&a));
    CBOR_CHECK(cbor_encode_uint(&m,10)); CBOR_CHECK(cbor_encoder_create_array(&m,&a,1));
    CBOR_CHECK(cbor_encoder_create_map(&a,&k,2));
    CBOR_CHECK(cbor_encode_text_stringz(&k,"alg")); CBOR_CHECK(cbor_encode_int(&k,-7));
    CBOR_CHECK(cbor_encode_text_stringz(&k,"type")); CBOR_CHECK(cbor_encode_text_stringz(&k,"public-key"));
    CBOR_CHECK(cbor_encoder_close_container(&a,&k)); CBOR_CHECK(cbor_encoder_close_container(&m,&a));
    CBOR_CHECK(cbor_encoder_close_container(&e,&m));
    apdu.rlen=(uint16_t)cbor_encoder_get_buffer_size(&e,apdu.rdata);
err:
    return error==CborNoError ? 0 : CTAP2_ERR_PROCESSING;
}
int cbor_get_assertion(const uint8_t *,size_t,bool);
size_t fv_fido_engine_command_channel(uint32_t channel, const uint8_t *req,size_t n,uint8_t *out,size_t cap) {
    request_frame.cid = channel;
    if (!out || cap < 1) return 0;
    out[0]=CTAP2_ERR_PROCESSING;
    if (n>FV_FIDO_ENGINE_MESSAGE_SIZE) { out[0]=CTAP1_ERR_INVALID_LEN; return 1; }
    if (!opened || failed || !req || !n) return 1;
    memset(response_buffer,0,sizeof(response_buffer));
    apdu.rdata=response_buffer+8; apdu.rlen=0;
    pin_uv_auth_token_tick();
    if (req[0]!=CTAP_GET_NEXT_ASSERTION) reset_gna_state();
    staging=true;
    int ret=CTAP1_ERR_INVALID_CMD;
    switch(req[0]) {
    case CTAP_GET_INFO: ret=n==1?engine_get_info():CTAP1_ERR_INVALID_LEN; break;
    case CTAP_MAKE_CREDENTIAL: ret=cbor_make_credential(req+1,n-1); break;
    case CTAP_GET_ASSERTION: ret=cbor_get_assertion(req+1,n-1,false); break;
    case CTAP_GET_NEXT_ASSERTION: ret=cbor_get_next_assertion(req+1,n-1); break;
    case CTAP_CLIENT_PIN: ret=cbor_client_pin(req+1,n-1); break;
    case CTAP_RESET:
        ret=n==1?cbor_reset():CTAP1_ERR_INVALID_LEN;
        if (!ret) { fv_pico_pin_session_clear(); fv_pico_cred_session_clear(); reset_gna_state(); }
        break;
    case CTAP_CREDENTIAL_MGMT: case 0x41: ret=cbor_cred_mgmt(req+1,n-1); break;
    }
    if (platform.cancelled && platform.cancelled(platform.context)) {
        reset_gna_state(); fv_pico_cred_session_clear(); ret=CTAP2_ERR_KEEPALIVE_CANCEL;
    }
    staging=false;
    if (!low_flash_commit_sync(0)) ret=CTAP2_ERR_PROCESSING;
    if (platform.cancelled && platform.cancelled(platform.context)) {
        reset_gna_state(); fv_pico_cred_session_clear(); ret=CTAP2_ERR_KEEPALIVE_CANCEL;
    }
    if (ret) { out[0]=(uint8_t)(ret<0 ? -ret : ret); return 1; }
    if ((size_t)apdu.rlen+1>cap) { out[0]=CTAP2_ERR_PROCESSING; return 1; }
    out[0]=0; memcpy(out+1,apdu.rdata,apdu.rlen); return (size_t)apdu.rlen+1;
}

size_t fv_fido_engine_command(const uint8_t *req,size_t n,uint8_t *out,size_t cap) {
    return fv_fido_engine_command_channel(1u, req, n, out, cap);
}
