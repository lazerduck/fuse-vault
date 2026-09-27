#ifndef FV_BENCH_H
#define FV_BENCH_H
#include "pico/util/queue.h"
#include "fuse_vault/benchmark.h"
extern queue_t commands,responses;
extern uint8_t bench_buffer[FV_BENCH_BUFFER_BYTES];
void bench_worker(void);
void bench_usb_poll(void);
#endif
