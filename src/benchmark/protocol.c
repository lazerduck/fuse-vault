#include "fuse_vault/benchmark.h"
#include <string.h>
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i));
}
static void put64(uint8_t *p,uint64_t n) {
    put32(p,(uint32_t)n); put32(p+4,(uint32_t)(n>>32));
}
bool fv_bench_decode(const uint8_t h[FV_BENCH_REQUEST_BYTES],fv_bench_request *r) {
    if(!h || !r || get32(h)!=FV_BENCH_MAGIC || get32(h+4)!=FV_BENCH_VERSION) return false;
    *r=(fv_bench_request){get32(h+8),get32(h+12),get32(h+16),get32(h+20),get32(h+24),get32(h+28)};
    if(r->op<FV_BENCH_INFO || r->op>FV_BENCH_AUTH_CONFIG) return false;
    if(r->op==FV_BENCH_WRITE)
        return r->blocks>0 && r->blocks<=64 && r->payload_bytes==r->blocks*512;
    return r->payload_bytes==0;
}
void fv_bench_encode(const fv_bench_response *r,uint8_t h[FV_BENCH_RESPONSE_BYTES]) {
    memset(h,0,FV_BENCH_RESPONSE_BYTES);
    put32(h,FV_BENCH_MAGIC); put32(h+4,FV_BENCH_VERSION);
    put32(h+8,r->op|UINT32_C(0x80000000)); put32(h+12,r->sequence);
    put32(h+16,r->status); put32(h+20,r->payload_bytes);
    put64(h+24,r->crypto_us); put64(h+32,r->sd_us); put64(h+40,r->setup_us);
    put64(h+48,r->capacity_blocks); put32(h+56,r->cpu_hz); put32(h+60,r->sd_hz);
    put64(h+64,r->hmac_us);put64(h+72,r->metadata_us);
    put64(h+80,r->metadata_reads);put64(h+88,r->metadata_writes);
    put32(h+96,r->hmac_backend);put32(h+100,r->hmac_self_test);
}
