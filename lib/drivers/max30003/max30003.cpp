#include "max30003.h"

#include <Arduino.h>

#include "hal_spi.h"

static bool encode_rate(uint16_t sps, uint32_t *bits)
{
  switch (sps) {
    case 512: *bits = 0; return true;
    case 256: *bits = 1; return true;
    case 128: *bits = 2; return true;
    default: return false;
  }
}

static bool encode_gain(uint8_t vv, uint32_t *bits)
{
  switch (vv) {
    case 20: *bits = 0; return true;
    case 40: *bits = 1; return true;
    case 80: *bits = 2; return true;
    case 160: *bits = 3; return true;
    default: return false;
  }
}

static bool encode_dlpf(uint8_t hz, uint32_t *bits)
{
  switch (hz) {
    case 0: *bits = 0; return true;
    case 40: *bits = 1; return true;
    case 100: *bits = 2; return true;
    case 150: *bits = 3; return true;
    default: return false;
  }
}

static bool encode_imag(uint8_t na, uint32_t *bits)
{
  switch (na) {
    case 0: *bits = 0; return true;
    case 5: *bits = 1; return true;
    case 10: *bits = 2; return true;
    case 20: *bits = 3; return true;
    case 50: *bits = 4; return true;
    case 100: *bits = 5; return true;
    default: return false;
  }
}

static bool encode_vth(uint16_t mv, uint32_t *bits)
{
  switch (mv) {
    case 300: *bits = 0; return true;
    case 400: *bits = 1; return true;
    case 450: *bits = 2; return true;
    case 500: *bits = 3; return true;
    default: return false;
  }
}

static inline int32_t decode_sample(uint32_t word)
{
  return ((int32_t)(word << 8)) >> 14;
}

void max30003_init_bus(void)
{
  hal_spi_init();
}

uint32_t max30003_read_reg(uint8_t reg)
{
  uint8_t tx[4] = {MAX30003_CMD_READ(reg), 0, 0, 0};
  uint8_t rx[4] = {0};
  hal_spi_select();
  hal_spi_transfer(tx, rx, sizeof(tx));
  hal_spi_deselect();
  return ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
}

void max30003_write_reg(uint8_t reg, uint32_t value)
{
  uint8_t tx[4] = {
    MAX30003_CMD_WRITE(reg),
    (uint8_t)(value >> 16),
    (uint8_t)(value >> 8),
    (uint8_t)value,
  };
  hal_spi_select();
  hal_spi_transfer(tx, NULL, sizeof(tx));
  hal_spi_deselect();
}

uint32_t max30003_read_info(void)
{
  return max30003_read_reg(MAX30003_REG_INFO);
}

uint32_t max30003_read_status(void)
{
  return max30003_read_reg(MAX30003_REG_STATUS);
}

void max30003_fifo_reset(void)
{
  max30003_write_reg(MAX30003_REG_FIFO_RST, 0);
}

void max30003_synch(void)
{
  max30003_write_reg(MAX30003_REG_SYNCH, 0);
}

bool max30003_configure(const max30003_config_t *cfg)
{
  uint32_t rate, gain, dlpf, imag, vth;
  if (!encode_rate(cfg->rate_sps, &rate) || !encode_gain(cfg->gain_vv, &gain) ||
      !encode_dlpf(cfg->dlpf_hz, &dlpf) || !encode_imag(cfg->dcloff_imag_na, &imag) ||
      !encode_vth(cfg->dcloff_vth_mv, &vth) || cfg->fifo_threshold < 1 ||
      cfg->fifo_threshold > MAX30003_FIFO_DEPTH) {
    return false;
  }

  max30003_write_reg(MAX30003_REG_SW_RST, 0);
  delay(10);

  uint32_t gen = MAX30003_GEN_FMSTR(MAX30003_FMSTR_32768) | MAX30003_GEN_EN_ECG |
                 MAX30003_GEN_EN_RBIAS(MAX30003_RBIAS_ECG) |
                 MAX30003_GEN_RBIASV(MAX30003_RBIASV_100M) | MAX30003_GEN_RBIASP |
                 MAX30003_GEN_RBIASN;
  if (imag != 0) {
    gen |= MAX30003_GEN_EN_DCLOFF(MAX30003_DCLOFF_ECG) | MAX30003_GEN_DCLOFF_IMAG(imag) |
           MAX30003_GEN_DCLOFF_VTH(vth);
  }
  max30003_write_reg(MAX30003_REG_CNFG_GEN, gen);
  delay(100);  // let the PLL and the input bias settle

  uint32_t emux = cfg->invert_polarity ? MAX30003_EMUX_POL : 0;
  max30003_write_reg(MAX30003_REG_CNFG_EMUX, emux);

  uint32_t ecg = MAX30003_ECG_RATE(rate) | MAX30003_ECG_GAIN(gain) | MAX30003_ECG_DLPF(dlpf);
  if (cfg->dhpf_enable) {
    ecg |= MAX30003_ECG_DHPF;
  }
  max30003_write_reg(MAX30003_REG_CNFG_ECG, ecg);

  max30003_write_reg(MAX30003_REG_MNGR_INT,
                     MAX30003_MNGR_EFIT(cfg->fifo_threshold - 1) | MAX30003_MNGR_CLR_SAMP);

  // Lead-off is deliberately not routed to INTB: DCLOFFINT stays asserted for
  // as long as an electrode is off, which would hold INTB low and mask the
  // FIFO edges. Lead-off is read from STATUS on every FIFO service instead.
  max30003_write_reg(MAX30003_REG_EN_INT, MAX30003_EN_EINT | MAX30003_EN_EOVF |
                                              MAX30003_INTB_TYPE(MAX30003_INTB_OPEN_DRAIN_PU));
  max30003_write_reg(MAX30003_REG_EN_INT2, MAX30003_INTB_TYPE(MAX30003_INTB_DISABLED));

  max30003_synch();
  max30003_fifo_reset();

  uint32_t readback = max30003_read_reg(MAX30003_REG_CNFG_ECG);
  return (readback & MAX30003_ECG_FIELDS_MASK) == ecg;
}

max30003_fifo_result_t max30003_read_fifo(int32_t *samples, uint8_t max_samples)
{
  max30003_fifo_result_t result = {0, false};
  uint8_t cmd = MAX30003_CMD_READ(MAX30003_REG_ECG_FIFO_BURST);
  uint8_t word_bytes[3];

  hal_spi_select();
  hal_spi_transfer(&cmd, NULL, 1);
  for (uint8_t i = 0; i < MAX30003_FIFO_DEPTH; i++) {
    hal_spi_transfer(NULL, word_bytes, sizeof(word_bytes));
    uint32_t word = ((uint32_t)word_bytes[0] << 16) | ((uint32_t)word_bytes[1] << 8) |
                    word_bytes[2];
    uint32_t etag = MAX30003_ETAG(word);

    if (etag == MAX30003_ETAG_OVERFLOW) {
      result.overflow = true;
      break;
    }
    if (etag == MAX30003_ETAG_EMPTY) {
      break;
    }
    if (result.count < max_samples) {
      samples[result.count++] = decode_sample(word);
    }
    if (etag == MAX30003_ETAG_VALID_EOF || etag == MAX30003_ETAG_FAST_EOF) {
      break;
    }
  }
  hal_spi_deselect();
  return result;
}
