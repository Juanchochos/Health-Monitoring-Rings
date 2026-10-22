#pragma once

#include <stddef.h>
#include <stdint.h>

// SPI bus dedicated to the MAX30003. Chip select is driven manually so a whole
// command + burst payload stays inside one CS-low window.

void hal_spi_init(void);

// Begin the SPI transaction and pull CS low.
void hal_spi_select(void);

// Release CS and end the SPI transaction.
void hal_spi_deselect(void);

// Full-duplex EasyDMA transfer while selected. Buffers must be in RAM, not
// flash. tx may be NULL (clocks out the over-read character); rx may be NULL.
void hal_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len);
