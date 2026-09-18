set(FV_PICO_ROOT ${CMAKE_CURRENT_LIST_DIR})
add_library(fuse_vault_fido_engine STATIC
    ${FV_PICO_ROOT}/port.c
    ${FV_PICO_ROOT}/engine/fido.c
    ${FV_PICO_ROOT}/engine/files.c
    ${FV_PICO_ROOT}/engine/defs.c
    ${FV_PICO_ROOT}/engine/cbor.c
    ${FV_PICO_ROOT}/engine/credential.c
    ${FV_PICO_ROOT}/engine/cbor_make_credential.c
    ${FV_PICO_ROOT}/engine/cbor_get_assertion.c
    ${FV_PICO_ROOT}/engine/cbor_client_pin.c
    ${FV_PICO_ROOT}/engine/cbor_reset.c
    ${FV_PICO_ROOT}/engine/cbor_cred_mgmt.c
    ${FV_PICO_ROOT}/engine/object_authorization.c
    ${FV_PICO_ROOT}/engine/object_provider.c
    ${FV_PICO_ROOT}/engine/resident_container.c
    ${FV_PICO_ROOT}/sdk/crypto_utils.c
    ${FV_PICO_ROOT}/sdk/tlv.c
    ${FV_PICO_ROOT}/sdk/fs/file.c
    ${FV_PICO_ROOT}/sdk/fs/flash.c
    ${FV_PICO_ROOT}/sdk/fs/object_container.c
    ${FV_PICO_ROOT}/sdk/fs/object_container_store.c
    ${FV_PICO_ROOT}/sdk/fs/object_crypto_provider.c
    ${FV_PICO_ROOT}/sdk/fs/object_policy.c
    ${FV_PICO_ROOT}/sdk/fs/object_store.c
    ${FV_PICO_ROOT}/sdk/fs/object_store_txn.c
    ${FV_PICO_ROOT}/sdk/fs/vault_container.c
    ${FV_PICO_ROOT}/../tinycbor/cborparser.c
    ${FV_PICO_ROOT}/../tinycbor/cborparser_dup_string.c
    ${FV_PICO_ROOT}/../tinycbor/cborvalidation.c
)
target_include_directories(fuse_vault_fido_engine PRIVATE
    ${FV_PICO_ROOT}/engine ${FV_PICO_ROOT}/sdk ${FV_PICO_ROOT}/sdk/fs
    ${FV_PICO_ROOT}/sdk/usb ${FV_PICO_ROOT}/sdk/rng ${FV_PICO_ROOT}/sdk/otp
    ${FV_PICO_ROOT}/../tinycbor)
target_include_directories(fuse_vault_fido_engine PUBLIC ${FV_PICO_ROOT}/../../include)
target_compile_definitions(fuse_vault_fido_engine PRIVATE FV_PICO_PORT=1
    USB_ITF_HID=1 USB_BUFFER_SIZE=4096u CBOR_NO_FLOATING_POINT
    ENABLE_POWER_ON_RESET=1)
target_compile_options(fuse_vault_fido_engine PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(fuse_vault_fido_engine PUBLIC fuse_vault_fido_probe)
