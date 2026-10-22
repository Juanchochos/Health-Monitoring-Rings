#include "spo2.h"

#include "dsp_util.h"

void spo2_init(spo2_t *s, float a, float b, float c, uint32_t pair_window_us)
{
  s->a = a;
  s->b = b;
  s->c = c;
  s->pair_window_us = pair_window_us;
  spo2_reset(s);
}

void spo2_reset(spo2_t *s)
{
  s->count = 0;
  s->pos = 0;
  s->have_red = false;
  s->have_ir = false;
}

float spo2_from_ratio(float a, float b, float c, float ratio)
{
  float v = a * ratio * ratio + b * ratio + c;
  if (v > 100.0f) v = 100.0f;
  if (v < 70.0f) v = 70.0f;
  return v;
}

static bool try_pair(spo2_t *s)
{
  if (!s->have_red || !s->have_ir) {
    return false;
  }
  const int32_t dt = (int32_t)(s->red.t_peak_us - s->ir.t_peak_us);
  const uint32_t adt = (uint32_t)(dt < 0 ? -dt : dt);
  if (adt > s->pair_window_us) {
    return false;
  }
  s->have_red = false;
  s->have_ir = false;

  if (s->red.dc <= 0.0f || s->ir.dc <= 0.0f || s->red.ac_pp <= 0.0f || s->ir.ac_pp <= 0.0f) {
    return false;
  }
  const float ratio = (s->red.ac_pp / s->red.dc) / (s->ir.ac_pp / s->ir.dc);
  s->r_hist[s->pos] = ratio;
  s->pos = (uint8_t)((s->pos + 1) % SPO2_HISTORY);
  if (s->count < SPO2_HISTORY) {
    s->count++;
  }
  return true;
}

bool spo2_add_red(spo2_t *s, const ppg_beat_t *beat)
{
  s->red = *beat;
  s->have_red = true;
  return try_pair(s);
}

bool spo2_add_ir(spo2_t *s, const ppg_beat_t *beat)
{
  s->ir = *beat;
  s->have_ir = true;
  return try_pair(s);
}

bool spo2_get(const spo2_t *s, float *spo2_pct, float *ratio)
{
  if (s->count < SPO2_MIN_BEATS) {
    return false;
  }
  const float r = dsp_median(s->r_hist, s->count);
  if (ratio) {
    *ratio = r;
  }
  if (spo2_pct) {
    *spo2_pct = spo2_from_ratio(s->a, s->b, s->c, r);
  }
  return true;
}
