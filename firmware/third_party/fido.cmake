# Pinned, locally vendored Pico FIDO discovery slice and TinyCBOR encoder.
add_library(fuse_vault_fido_probe STATIC
    ${CMAKE_CURRENT_LIST_DIR}/pico_fido/get_info.c
    ${CMAKE_CURRENT_LIST_DIR}/tinycbor/cborencoder.c
    ${CMAKE_CURRENT_LIST_DIR}/tinycbor/cborencoder_close_container_checked.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/fido_probe.c
)
target_include_directories(fuse_vault_fido_probe PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/tinycbor
)
target_include_directories(fuse_vault_fido_probe PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include
)
target_compile_definitions(fuse_vault_fido_probe PRIVATE CBOR_NO_FLOATING_POINT)
target_compile_options(fuse_vault_fido_probe PRIVATE -Wall -Wextra)
