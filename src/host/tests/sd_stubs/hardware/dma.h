#pragma once
#include "hw_config.h"
typedef struct {uint32_t ints1;} fake_dma_t;
extern fake_dma_t native_test_dma;
#define dma_hw (&native_test_dma)
void dma_channel_set_irq1_enabled(uint,bool);
void dma_channel_abort(uint);
