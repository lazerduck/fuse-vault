#define _POSIX_C_SOURCE 200809L
#include "fuse_vault/crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) exit(1);
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
int main(void) {
    uint8_t keys[2][64], data[512]={0};
    for (unsigned j=0;j<2;j++) for (unsigned i=0;i<64;i++) keys[j][i]=(uint8_t)(i+j*71);
    const fv_algorithm configurations[][2]={{FV_AES_256_XTS,0},{FV_CAMELLIA_256_XTS,0},
        {FV_AES_256_XTS,FV_CAMELLIA_256_XTS},{FV_CAMELLIA_256_XTS,FV_AES_256_XTS}};
    const char *names[]={"AES-256-XTS","Camellia-256-XTS","AES -> Camellia","Camellia -> AES"};
    puts("Desktop RAM benchmark: public test keys, software ciphers, no authentication or I/O");
    for (unsigned c=0;c<4;c++) {
        fv_pipeline p={0}; double start=now();
        for (unsigned repeat=0;repeat<1000;repeat++)
            if (fv_pipeline_init(&p,configurations[c],(const uint8_t (*)[64])keys,c<2?1:2)) return 1;
        printf("%s: setup %.2f us",names[c],(now()-start)*1e6/1000);
        for (int enc=1;enc>=0;enc--) {
            start=now();
            for (uint64_t lba=0;lba<32768;lba++) {
                fv_status s=enc?fv_pipeline_encrypt(&p,lba,data,data):fv_pipeline_decrypt(&p,lba,data,data);
                if (s!=FV_OK) return 1;
            }
            printf(", %s %.2f MiB/s",enc?"encrypt":"decrypt",16.0/(now()-start));
        }
        printf(" (checksum %u)\n",data[0]);
        fv_pipeline_clear(&p);
    }
    return 0;
}
