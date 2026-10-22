#include "max30102.h"

#include <Arduino.h>

#include "board_config.h"
#include "hal_i2c.h"

static bool encode_sample_rate(uint16_t hz, uint8_t *bits)
{
  static const uint16_t kRates[] = {50, 100, 200, 400, 800, 1000, 1600, 3200};
  for (uint8_t i = 0; i < sizeof(kRates) / sizeof(kRates[0]); i++) {
    if (kRates[i] == hz) {
      *bits = i;
      return true;
    }
  }
  return false;
}

static bool encode_pulse_width(uint16_t us, uint8_t *bits)
{
  switch (us) {
    case 69: *bits = 0; return true;
    case 118: *bits = 1; return true;
    case 215: *bits = 2; return true;
    case 411: *bits = 3; return true;
    default: return false;
  }
}

static bool encode_adc_range(uint16_t na, uint8_t *bits)
{
  switch (na) {
    case 2048: *bits = 0; return true;
    case 4096: *bits = 1; return true;
    case 8192: *bits = 2; return true;
    case 16384: *bits = 3; return true;
    default: return false;
  }
}

static bool encode_sample_avg(uint8_t n, uint8_t *bits)
{
  for (uint8_t i = 0; i <= 5; i++) {
    if ((1u << i) == n) {
      *bits = i;
      return true;
    }
  }
  return false;
}

static inline uint32_t decode_sample(const uint8_t *p)
{
  return (((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]) & MAX30102_SAMPLE_MASK;
}

void max30102_init_bus(void)
{
  hal_i2c_init();
}

bool max30102_read_reg(uint8_t reg, uint8_t *value)
{
  return hal_i2c_read_regs(PPG_I2C_ADDR, reg, value, 1);
}

bool max30102_write_reg(uint8_t reg, uint8_t value)
{
  return hal_i2c_write_reg(PPG_I2C_ADDR, reg, value);
}

bool max30102_read_part_id(uint8_t *part_id, uint8_t *rev_id)
{
  return max30102_read_reg(MAX30102_REG_PART_ID, part_id) &&
         max30102_read_reg(MAX30102_REG_REV_ID, rev_id);
}

bool max30102_read_int_status(uint8_t *status)
{
  return max30102_read_reg(MAX30102_REG_INT_STATUS_1, status);
}

bool max30102_configure(const max30102_config_t *cfg)
{
  uint8_t sr, pw, rge, avg;
  if (!encode_sample_rate(cfg->sample_rate_hz, &sr) ||
      !encode_pulse_width(cfg->pulse_width_us, &pw) ||
      !encode_adc_range(cfg->adc_range_na, &rge) || !encode_sample_avg(cfg->sample_avg, &avg) ||
      cfg->fifo_a_full > 15) {
    return false;
  }

  if (!max30102_write_reg(MAX30102_REG_MODE_CONFIG, MAX30102_MODE_RESET)) {
    return false;
  }
  uint32_t start = millis();
  uint8_t mode = MAX30102_MODE_RESET;
  while (mode & MAX30102_MODE_RESET) {
    if (millis() - start > 100 || !max30102_read_reg(MAX30102_REG_MODE_CONFIG, &mode)) {
      return false;
    }
  }
  uint8_t status;
  max30102_read_int_status(&status);

  const uint8_t fifo_cfg = MAX30102_FIFO_SMP_AVE(avg) | MAX30102_FIFO_A_FULL(cfg->fifo_a_full);
  const uint8_t spo2_cfg =
      MAX30102_SPO2_ADC_RGE(rge) | MAX30102_SPO2_SR(sr) | MAX30102_SPO2_LED_PW(pw);

  bool ok = max30102_write_reg(MAX30102_REG_FIFO_CONFIG, fifo_cfg) &&
            max30102_write_reg(MAX30102_REG_SPO2_CONFIG, spo2_cfg) &&
            max30102_write_reg(MAX30102_REG_LED1_PA, cfg->led_red_pa) &&
            max30102_write_reg(MAX30102_REG_LED2_PA, cfg->led_ir_pa) &&
            max30102_write_reg(MAX30102_REG_INT_ENABLE_1, MAX30102_INT_A_FULL) &&
            max30102_write_reg(MAX30102_REG_INT_ENABLE_2, 0) &&
            max30102_write_reg(MAX30102_REG_FIFO_WR_PTR, 0) &&
            max30102_write_reg(MAX30102_REG_OVF_COUNTER, 0) &&
            max30102_write_reg(MAX30102_REG_FIFO_RD_PTR, 0) &&
            max30102_write_reg(MAX30102_REG_MODE_CONFIG, MAX30102_MODE_SPO2);
  if (!ok) {
    return false;
  }

  uint8_t fifo_rb = 0, spo2_rb = 0;
  return max30102_read_reg(MAX30102_REG_FIFO_CONFIG, &fifo_rb) &&
         max30102_read_reg(MAX30102_REG_SPO2_CONFIG, &spo2_rb) && fifo_rb == fifo_cfg &&
         (spo2_rb & 0x7F) == spo2_cfg;
}

max30102_fifo_result_t max30102_read_fifo(uint32_t *red, uint32_t *ir, uint8_t max_samples)
{
  max30102_fifo_result_t result = {0, 0, false};

  uint8_t ptrs[3];  // FIFO_WR_PTR, OVF_COUNTER, FIFO_RD_PTR
  if (!hal_i2c_read_regs(PPG_I2C_ADDR, MAX30102_REG_FIFO_WR_PTR, ptrs, sizeof(ptrs))) {
    return result;
  }
  uint8_t wr = ptrs[0] & 0x1F;
  uint8_t ovf = ptrs[1] & 0x1F;
  uint8_t rd = ptrs[2] & 0x1F;

  uint8_t available = (uint8_t)((wr - rd) & 0x1F);
  if (available == 0 && ovf != 0) {
    available = MAX30102_FIFO_DEPTH;
  }
  result.overflow = ovf;

  uint8_t n = available < max_samples ? available : max_samples;
  uint8_t raw[MAX30102_FIFO_DEPTH * MAX30102_BYTES_PER_SAMPLE];
  if (n > 0 && !hal_i2c_read_stream(PPG_I2C_ADDR, MAX30102_REG_FIFO_DATA, raw,
                                    (size_t)n * MAX30102_BYTES_PER_SAMPLE)) {
    return result;
  }

  for (uint8_t i = 0; i < n; i++) {
    const uint8_t *p = &raw[i * MAX30102_BYTES_PER_SAMPLE];
    red[i] = decode_sample(p);
    ir[i] = decode_sample(p + 3);
  }
  result.count = n;
  result.ok = true;
  return result;
}
