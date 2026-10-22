#pragma once

#include <stddef.h>
#include <stdint.h>

// I2C bus used by the MAX30102. All functions return false on a NACK or a
// short read.

void hal_i2c_init(void);

bool hal_i2c_probe(uint8_t addr);

bool hal_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value);

// Read len bytes starting at reg with register auto-increment.
bool hal_i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);

// Read len bytes from a single streaming register (e.g. a FIFO data port),
// re-addressing the same register for every chunk of I2C_MAX_CHUNK_BYTES.
bool hal_i2c_read_stream(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);
