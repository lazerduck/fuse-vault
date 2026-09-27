/* Contract simulation only: target self-test is needed to verify silicon output. */
#include "pico/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) do{if(!(x)){fprintf(stderr,"SHA SDK contract violation line %d\n",__LINE__);abort();}}while(0)
unsigned fake_starts,fake_finishes,fake_abandons,fake_updates,fake_fail_start;
bool fake_busy,fake_error;
int pico_sha256_try_start(pico_sha256_state_t *s,enum sha256_endianness order,bool dma) {
    REQUIRE(order==SHA256_BIG_ENDIAN && !dma);
    memset(s,0,sizeof(*s));fake_starts++;
    if(fake_busy || fake_starts==fake_fail_start)return -1;
    fake_busy=s->locked=true;
    mbedtls_sha256_init(&s->hash);REQUIRE(mbedtls_sha256_starts(&s->hash,0)==0);return PICO_OK;
}
void pico_sha256_update_blocking(pico_sha256_state_t *s,const uint8_t *p,size_t n) {
    REQUIRE(s->locked && fake_busy && p && n);fake_updates++;
    REQUIRE(mbedtls_sha256_update(&s->hash,p,n)==0);
}
void pico_sha256_finish(pico_sha256_state_t *s,sha256_result_t *r) {
    REQUIRE(s->locked && fake_busy);fake_finishes++;
    if(r)REQUIRE(mbedtls_sha256_finish(&s->hash,r->bytes)==0);else fake_abandons++;
    mbedtls_sha256_free(&s->hash);s->locked=fake_busy=false;
}
bool sha256_err_not_ready(void){return fake_error;}
