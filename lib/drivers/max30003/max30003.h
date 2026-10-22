#pragma once

#include <stdint.h>

#include "max30003_regs.h"

typedef struct {
  uint16_t rate_sps;        // 128, 256, 512
  uint8_t gain_vv;          // 20, 40, 80, 160
  bool dhpf_enable;         // on-chip 0.5 Hz high-pass
  uint8_t dlpf_hz;          // 0 (bypass), 40, 100, 150
  bool invert_polarity;
  uint8_t dcloff_imag_na;   // 0 disables DC lead-off; else 5, 10, 20, 50, 100
  uint16_t dcloff_vth_mv;   // 300, 400, 450, 500
  uint8_t fifo_threshold;   // INTB asserts at this many unread samples (1-32)
} max30003_config_t;

typedef struct {
  uint8_t count;            // valid samples written to the output array
  bool overflow;            // FIFO overflowed; caller should max30003_fifo_reset()
} max30003_fifo_result_t;

void max30003_init_bus(void);

uint32_t max30003_read_reg(uint8_t reg);
void max30003_write_reg(uint8_t reg, uint32_t value);

uint32_t max30003_read_info(void);

// Soft reset, then program CNFG_GEN, CNFG_EMUX, CNFG_ECG, MNGR_INT and EN_INT,
// then SYNCH and FIFO_RST. Returns false on an unsupported setting or if the
// CNFG_ECG read-back does not match.
bool max30003_configure(const max30003_config_t *cfg);

uint32_t max30003_read_status(void);
void max30003_fifo_reset(void);
void max30003_synch(void);

// Burst-read the ECG FIFO until ETAG reports end-of-FIFO, empty, or overflow.
max30003_fifo_result_t max30003_read_fifo(int32_t *samples, uint8_t max_samples);

static inline bool max30003_status_lead_off(uint32_t status)
{
  return (status & MAX30003_STATUS_LDOFF_MASK) != 0;
}
