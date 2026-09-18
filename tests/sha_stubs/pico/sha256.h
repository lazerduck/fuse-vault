#ifndef FV_TEST_SHA_SDK_H
#define FV_TEST_SHA_SDK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <mbedtls/sha256.h>
#define PICO_OK 0
enum sha256_endianness { SHA256_LITTLE_ENDIAN,SHA256_BIG_ENDIAN };
typedef union {uint32_t words[8];uint8_t bytes[32];} sha256_result_t;
typedef struct {mbedtls_sha256_context hash;bool locked;} pico_sha256_state_t;
int pico_sha256_try_start(pico_sha256_state_t *,enum sha256_endianness,bool);
void pico_sha256_update_blocking(pico_sha256_state_t *,const uint8_t *,size_t);
void pico_sha256_finish(pico_sha256_state_t *,sha256_result_t *);
bool sha256_err_not_ready(void);
extern unsigned fake_starts,fake_finishes,fake_abandons,fake_updates,fake_fail_start;
extern bool fake_busy,fake_error;
#endif
