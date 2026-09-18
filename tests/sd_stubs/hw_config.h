#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef unsigned uint;
#define pio1 ((void*)1)
#define DMA_IRQ_1 1
#define STA_NOINIT 1
#define SD_IF_SDIO 2
#define SDIO_MAX_BLOCKS 256
#define SDIO_OK 0
#define SD_BLOCK_DEVICE_ERROR_NONE 0
struct sd_card_t;
typedef struct {
 unsigned CMD_gpio,D0_gpio,DMA_IRQ_num,baud_rate;
 void *SDIO_PIO;
 struct {bool resources_claimed,ongoing_wr_mlt_blk;int SDIO_DMA_CH,SDIO_DMA_CHB,SDIO_DATA_SM,SDIO_CMD_SM;uint32_t ocr,rca;} state;
} sd_sdio_if_t;
typedef struct sd_card_t {
 int type;sd_sdio_if_t *sdio_if_p;
 struct {unsigned m_Status;uint32_t sectors;} state;
 unsigned (*init)(struct sd_card_t*);
 void (*deinit)(struct sd_card_t*);
 int (*read_blocks)(struct sd_card_t*,uint8_t*,uint32_t,uint32_t);
 int (*write_blocks)(struct sd_card_t*,const uint8_t*,uint32_t,uint32_t);
 int (*sync)(struct sd_card_t*);
} sd_card_t;
bool sd_init_driver(void);
sd_card_t *sd_get_by_num(size_t);
void pio_sm_set_enabled(void*,uint,bool);
