#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "ring_buffer.h"

// Acquisition layer: owns both sensor buses, services the INTB/INT edges,
// timestamps every sample and pushes into the three SPSC ring buffers.

struct acq_stats_t {
  uint32_t ecg_samples;          // cumulative samples pushed
  uint32_t ppg_samples;
  uint32_t ecg_rb_overflows;     // ring buffer full (consumer too slow)
  uint32_t ppg_rb_overflows;
  uint32_t ecg_fifo_overflows;   // sensor FIFO overflowed (acquisition too slow)
  uint32_t ppg_fifo_overflows;
  uint32_t ecg_gaps;             // consecutive timestamps off by > 1.5 Ts or < 0.5 Ts
  uint32_t ppg_gaps;
  uint32_t ecg_polled_reads;     // reads without a trustworthy edge
  uint32_t ppg_polled_reads;
  uint32_t ecg_latency_max_us;   // edge to start of burst read
  uint32_t ppg_latency_max_us;
  uint32_t ecg_latency_sum_us;
  uint32_t ppg_latency_sum_us;
  uint32_t ecg_edges;
  uint32_t ppg_edges;
  uint32_t i2c_errors;
  float ecg_period_us;           // tracked sensor sample period
  float ppg_period_us;
};

// Initialize ring buffers and timestamp reconstruction. Call after the sensors
// are configured and hal_timestamp_init() has run.
void acq_init();

// Attach the timestamped interrupts and start the acquisition task. `consumer`
// receives a task notification after every batch pushed (may be NULL).
bool acq_start(TaskHandle_t consumer);

ring_buffer_t *acq_rb_ecg();
ring_buffer_t *acq_rb_ppg_red();
ring_buffer_t *acq_rb_ppg_ir();

bool acq_ecg_contact();
void acq_get_stats(acq_stats_t *out);
