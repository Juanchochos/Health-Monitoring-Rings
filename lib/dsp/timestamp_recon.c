#include "timestamp_recon.h"

#include <math.h>

void ts_recon_init(ts_recon_t *r, float sample_rate_hz, uint32_t threshold, int32_t latency_us,
                   float alpha, float max_deviation)
{
  r->ts_nominal_us = 1e6f / sample_rate_hz;
  r->ts_est_us = r->ts_nominal_us;
  r->alpha = alpha;
  r->max_deviation = max_deviation;
  r->threshold = threshold > 0 ? threshold : 1;
  r->latency_us = latency_us;
  r->total_samples = 0;
  ts_recon_reset(r);
}

void ts_recon_reset(ts_recon_t *r)
{
  r->have_edge = false;
  r->have_last = false;
}

static void finish_batch(ts_recon_t *r, uint32_t n, const uint32_t *out_t)
{
  r->total_samples += n;
  if (n > 0) {
    r->last_t_us = out_t[n - 1];
    r->have_last = true;
  }
}

void ts_recon_batch_edge(ts_recon_t *r, uint32_t t_edge_us, uint32_t n, uint32_t *out_t)
{
  const uint32_t edge_index = r->total_samples + r->threshold - 1;

  if (r->have_edge && edge_index > r->last_edge_index) {
    const float span = (float)(edge_index - r->last_edge_index);
    const float measured = (float)(uint32_t)(t_edge_us - r->last_edge_us) / span;
    if (fabsf(measured - r->ts_nominal_us) <= r->max_deviation * r->ts_nominal_us) {
      r->ts_est_us += r->alpha * (measured - r->ts_est_us);
    }
  }
  r->last_edge_us = t_edge_us;
  r->last_edge_index = edge_index;
  r->have_edge = true;

  const float k = (float)(r->threshold - 1);
  for (uint32_t i = 0; i < n; i++) {
    const float offset = ((float)i - k) * r->ts_est_us;
    out_t[i] = t_edge_us + (uint32_t)(int32_t)lrintf(offset) - (uint32_t)r->latency_us;
  }
  finish_batch(r, n, out_t);
}

void ts_recon_batch_polled(ts_recon_t *r, uint32_t t_read_us, uint32_t n, uint32_t *out_t)
{
  for (uint32_t i = 0; i < n; i++) {
    float offset;
    uint32_t base;
    if (r->have_last) {
      base = r->last_t_us;
      offset = (float)(i + 1) * r->ts_est_us;
    } else {
      base = t_read_us - (uint32_t)r->latency_us;
      offset = -(float)(n - 1 - i) * r->ts_est_us;
    }
    out_t[i] = base + (uint32_t)(int32_t)lrintf(offset);
  }
  finish_batch(r, n, out_t);
}

float ts_recon_period_us(const ts_recon_t *r)
{
  return r->ts_est_us;
}
