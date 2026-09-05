#ifndef FUSE_VAULT_MBEDTLS_CONFIG_H
#define FUSE_VAULT_MBEDTLS_CONFIG_H

#include <limits.h>

/* Minimal Mbed TLS surface used by the credential envelope. */
#define MBEDTLS_AES_C
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_CIPHER_C
#define MBEDTLS_GCM_C

#endif
