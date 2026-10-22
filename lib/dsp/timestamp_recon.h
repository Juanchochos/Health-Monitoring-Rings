#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reconstructs a microsecond timestamp for every FIFO sample of one sensor.
//
// The sensor raises its interrupt when the `threshold`-th unread sample lands,
// and the edge time is hardware-captured. Because the acquisition task always
// drains the FIFO, that sample has global index total_samples + threshold - 1,
// so a batch read after an edge is timed as
//   t_i = t_edge + (i - (threshold - 1)) * Ts_est - latency_us.
// Ts_est tracks the sensor's real oscillator from consecutive edges
// ((t_edge_n - t_edge_n-1) / samples_between, EMA-smoothed), rejecting
// measurements that deviate from nominal by more than max_deviation.
typedef struct {
  float ts_nominal_us;
  float ts_est_us;
  float alpha;
  float max_deviation;
  uint32_t threshold;
  int32_t latency_us;

  uint32_t total_samples;
  uint32_t last_edge_us;
  uint32_t last_edge_index;
  bool have_edge;

  uint32_t last_t_us;
  bool have_last;
} ts_recon_t;

void ts_recon_init(ts_recon_t *r, float sample_rate_hz, uint32_t threshold, int32_t latency_us,
                   float alpha, float max_deviation);

// Forget continuity (e.g. after a FIFO overflow/reset); keeps Ts_est.
void ts_recon_reset(ts_recon_t *r);

// Batch read in response to an edge captured at t_edge_us.
void ts_recon_batch_edge(ts_recon_t *r, uint32_t t_edge_us, uint32_t n, uint32_t *out_t);

// Batch read without a trustworthy edge (fallback poll). Continues from the
// previous sample when possible, otherwise back-fills from t_read_us.
void ts_recon_batch_polled(ts_recon_t *r, uint32_t t_read_us, uint32_t n, uint32_t *out_t);

float ts_recon_period_us(const ts_recon_t *r);

#ifdef __cplusplus
}
#endif
