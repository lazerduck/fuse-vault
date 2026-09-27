#include <stddef.h>
#include <stdint.h>
#define spi0 ((void *)0)
#define SPI_CPOL_0 0
#define SPI_CPHA_0 0
#define SPI_MSB_FIRST 0
unsigned spi_init(void *spi, unsigned baud);
void spi_set_format(void *spi, unsigned bits, unsigned cpol, unsigned cpha, unsigned order);
int spi_write_blocking(void *spi, const uint8_t *data, size_t size);
