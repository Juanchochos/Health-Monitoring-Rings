#pragma once

#include <stdint.h>

#include "max30102_regs.h"

typedef struct {
  uint16_t sample_rate_hz;  // 50, 100, 200, 400, 800, 1000, 1600, 3200
  uint16_t pulse_width_us;  // 69, 118, 215, 411
  uint16_t adc_range_na;    // 2048, 4096, 8192, 16384
  uint8_t led_red_pa;       // 0.2 mA per LSB
  uint8_t led_ir_pa;
  uint8_t sample_avg;       // 1, 2, 4, 8, 16, 32
  uint8_t fifo_a_full;      // free slots remaining when A_FULL fires (0-15)
} max30102_config_t;

typedef struct {
  uint8_t count;            // samples written to red[] / ir[]
  uint8_t overflow;         // samples lost since the previous read (OVF_COUNTER)
  bool ok;                  // false on an I2C error
} max30102_fifo_result_t;

void max30102_init_bus(void);

bool max30102_read_reg(uint8_t reg, uint8_t *value);
bool max30102_write_reg(uint8_t reg, uint8_t value);

bool max30102_read_part_id(uint8_t *part_id, uint8_t *rev_id);

// Soft reset, then program FIFO_CONFIG, SPO2_CONFIG, LED currents and
// A_FULL interrupt, clear the FIFO pointers and enter SpO2 mode. Returns false
// on an unsupported setting, reset timeout, or read-back mismatch.
bool max30102_configure(const max30102_config_t *cfg);

// Reading INT_STATUS_1 releases the INT pin.
bool max30102_read_int_status(uint8_t *status);

max30102_fifo_result_t max30102_read_fifo(uint32_t *red, uint32_t *ir, uint8_t max_samples);
