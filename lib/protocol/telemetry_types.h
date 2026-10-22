#pragma once

#include <stdint.h>

#include "vitals.h"

#ifdef __cplusplus
extern "C" {
#endif

// One ECG sample with the most recent PPG sample held alongside it (the
// Phase 2/3 "timestamp_us,ecg,red,ir" row).
typedef struct {
  uint32_t timestamp_us;
  int32_t ecg;
  int32_t red;
  int32_t ir;
} raw_record_t;

// Periodic health of the acquisition path.
typedef struct {
  uint8_t flags;               // VITALS_FLAG_* snapshot
  uint32_t ecg_fifo_overflows;
  uint32_t ppg_fifo_overflows;
  uint32_t rb_overflows;       // ECG + PPG ring buffer drops
  uint32_t gaps;               // ECG + PPG timestamp gaps
  uint16_t i2c_errors;
  float ecg_rate_hz;
  float ppg_rate_hz;
} telemetry_status_t;

#ifdef __cplusplus
}
#endif
