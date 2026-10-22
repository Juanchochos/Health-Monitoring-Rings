#include "ppg_processor.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PPG_HIST_MASK (PPG_HISTORY - 1)

// Least-squares parabola half-widths (samples) for the sub-sample fits.
#ifndef PPG_PEAK_FIT_K
#define PPG_PEAK_FIT_K 2
#endif
#ifndef PPG_FOOT_FIT_K
#define PPG_FOOT_FIT_K 2
#endif
#define PPG_FIT_MAX_K (PPG_PEAK_FIT_K > PPG_FOOT_FIT_K ? PPG_PEAK_FIT_K : PPG_FOOT_FIT_K)

static uint32_t seconds_to_samples(float fs, float s)
{
  return (uint32_t)lroundf(fs * s);
}

void ppg_processor_init(ppg_processor_t *p, const ppg_processor_config_t *cfg)
{
  memset(p, 0, sizeof(*p));
  p->fs = cfg->fs;
  p->ts_us = 1e6f / cfg->fs;
  p->dc_alpha = 1.0f - expf(-2.0f * (float)M_PI * cfg->dc_lpf_hz / cfg->fs);

  biquad_cascade_init(&p->bp);
  biquad_cascade_add_butter_highpass(&p->bp, cfg->fs, cfg->bp_lo_hz, 2);
  biquad_cascade_add_butter_lowpass(&p->bp, cfg->fs, cfg->bp_hi_hz, 2);

  const float timing_hz = cfg->timing_lpf_hz > 0.0f ? cfg->timing_lpf_hz : cfg->bp_hi_hz;
  biquad_cascade_init(&p->lp);
  biquad_cascade_add_butter_lowpass(&p->lp, cfg->fs, timing_hz, 2);
  p->lp_delay_samples = biquad_cascade_group_delay(&p->lp, cfg->fs, cfg->delay_ref_hz);

  p->refractory = seconds_to_samples(cfg->fs, 0.300f);
  p->settle = seconds_to_samples(cfg->fs, 0.080f);
  p->timeout = seconds_to_samples(cfg->fs, 2.0f);
  p->learn_len = seconds_to_samples(cfg->fs, cfg->learning_s > 0 ? cfg->learning_s : 2.0f);
  ppg_processor_reset(p);
}

void ppg_processor_reset(ppg_processor_t *p)
{
  p->primed = false;
  p->learning = true;
  p->learn_end = p->n + p->learn_len;
  p->learn_min = INFINITY;
  p->learn_max = -INFINITY;
  p->seeking_peak = false;
  p->have_peak = false;
  p->pending = false;
  p->amp_ema = 0.0f;
}

float ppg_processor_dc(const ppg_processor_t *p)
{
  return p->dc;
}

static float lp_at(const ppg_processor_t *p, int64_t i)
{
  return p->hist_lp[i & PPG_HIST_MASK];
}

static float slope_at(const ppg_processor_t *p, int64_t i)
{
  return 0.5f * (lp_at(p, i + 1) - lp_at(p, i - 1));
}

// Vertex offset of the least-squares parabola through y[0..2k] (centred on
// y[k]); 0 if the fit is not concave.
static float ls_vertex(const float *y, int k)
{
  const float n = (float)(2 * k + 1);
  const float su2 = (float)(k * (k + 1) * (2 * k + 1)) / 3.0f;
  const float su4 = (float)(k * (k + 1) * (2 * k + 1) * (3 * k * k + 3 * k - 1)) / 15.0f;
  float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;
  for (int i = 0; i <= 2 * k; i++) {
    const float u = (float)(i - k);
    s0 += y[i];
    s1 += u * y[i];
    s2 += u * u * y[i];
  }
  const float a1 = s1 / su2;
  const float a2 = (s2 - su2 / n * s0) / (su4 - su2 * su2 / n);
  if (a2 >= 0.0f) {
    return 0.0f;
  }
  float frac = -a1 / (2.0f * a2);
  if (frac > 1.0f) frac = 1.0f;
  if (frac < -1.0f) frac = -1.0f;
  return frac;
}

static uint32_t time_at(const ppg_processor_t *p, int64_t i, float frac)
{
  const float offset_us = (frac - p->lp_delay_samples) * p->ts_us;
  return p->hist_t[i & PPG_HIST_MASK] + (uint32_t)(int32_t)lrintf(offset_us);
}

static int64_t clamp64(int64_t v, int64_t lo, int64_t hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}

// newest: index of the most recent sample in the history.
static void finalize(ppg_processor_t *p, int64_t newest, ppg_beat_t *beat)
{
  const int64_t oldest = newest - PPG_HISTORY + 1;
  const int64_t w = (int64_t)seconds_to_samples(p->fs, 0.1f);
  const int64_t bp_peak = clamp64((int64_t)p->pending_peak_idx, oldest, newest);

  // The baseline is anchored at this beat's and the next beat's bandpass
  // troughs: both sit at the same pulse phase, so the line's slope is the
  // baseline slope even when the tilt moves the low-pass minima.
  const int64_t a = clamp64((int64_t)p->pending_trough_idx, oldest, bp_peak);
  int64_t b = newest;
  float m = 0.0f;
  if (p->pending_have_next) {
    b = clamp64((int64_t)p->pending_next_trough_idx, bp_peak + 1, newest);
    if (b > a) {
      m = (lp_at(p, b) - lp_at(p, a)) / (float)(b - a);
    }
  }

  // Systolic peak on the detrended low-pass signal.
  const int64_t pk_lo = clamp64(bp_peak - (int64_t)seconds_to_samples(p->fs, 0.05f), a + 1,
                                newest - 2);
  const int64_t pk_hi = clamp64(b, pk_lo, newest - 2);
  int64_t peak = pk_lo;
  float peak_v = lp_at(p, peak) - m * (float)(peak - a);
  for (int64_t i = pk_lo + 1; i <= pk_hi; i++) {
    const float v = lp_at(p, i) - m * (float)(i - a);
    if (v > peak_v) {
      peak_v = v;
      peak = i;
    }
  }
  float y[2 * PPG_FIT_MAX_K + 1];
  for (int k = 0; k <= 2 * PPG_PEAK_FIT_K; k++) {
    const int64_t i = clamp64(peak - PPG_PEAK_FIT_K + k, oldest, newest);
    y[k] = lp_at(p, i) - m * (float)(i - a);
  }
  const float peak_frac = ls_vertex(y, PPG_PEAK_FIT_K);

  // Foot: steepest upslope between the beat's trough and its peak. A constant
  // baseline slope does not move the maximum, so no detrend is needed.
  const int64_t ft_lo = clamp64(a - w, oldest + 1 + PPG_FOOT_FIT_K, newest - 1 - PPG_FOOT_FIT_K);
  const int64_t ft_hi = clamp64(peak - 1, ft_lo, newest - 1 - PPG_FOOT_FIT_K);
  int64_t foot = ft_lo;
  for (int64_t i = ft_lo + 1; i <= ft_hi; i++) {
    if (slope_at(p, i) > slope_at(p, foot)) {
      foot = i;
    }
  }
  for (int k = 0; k <= 2 * PPG_FOOT_FIT_K; k++) {
    y[k] = slope_at(p, foot - PPG_FOOT_FIT_K + k);
  }
  const float foot_frac = ls_vertex(y, PPG_FOOT_FIT_K);

  beat->t_peak_us = time_at(p, peak, peak_frac);
  beat->t_foot_us = time_at(p, foot, foot_frac);
  beat->ac_pp = p->pending_ac;
  beat->dc = p->pending_dc;
}

bool ppg_processor_process(ppg_processor_t *p, float raw, uint32_t t_us, ppg_beat_t *beat)
{
  const float x = -raw;
  if (!p->primed) {
    p->dc = raw;
    biquad_cascade_prime(&p->bp, x);
    biquad_cascade_prime(&p->lp, x);
    p->primed = true;
  }
  p->dc += p->dc_alpha * (raw - p->dc);
  const float yb = biquad_cascade_process(&p->bp, x);
  const float yl = biquad_cascade_process(&p->lp, x);

  const uint32_t n = p->n;
  p->hist_lp[n & PPG_HIST_MASK] = yl;
  p->hist_t[n & PPG_HIST_MASK] = t_us;

  bool emitted = false;
  if (p->pending && ((p->pending_have_next && n >= p->pending_ready_idx) ||
                     n - p->pending_peak_idx >= p->timeout)) {
    finalize(p, (int64_t)n, beat);
    p->pending = false;
    emitted = true;
  }

  if (p->learning) {
    if (yb < p->learn_min) p->learn_min = yb;
    if (yb > p->learn_max) p->learn_max = yb;
    if (n >= p->learn_end) {
      p->amp_ema = 0.5f * (p->learn_max - p->learn_min);
      p->learning = false;
      p->trough_val = yb;
      p->trough_idx = n;
      p->last_peak_idx = n;
    }
  } else {
    const float floor = 1e-4f * fabsf(p->dc);
    const float thr = fmaxf(0.3f * p->amp_ema, floor);

    if (!p->seeking_peak) {
      if (yb < p->trough_val) {
        p->trough_val = yb;
        p->trough_idx = n;
      } else if (yb > p->trough_val + thr) {
        p->seeking_peak = true;
        p->peak_val = yb;
        p->peak_idx = n;
        if (p->pending && !p->pending_have_next) {
          p->pending_have_next = true;
          p->pending_next_trough_idx = p->trough_idx;
          p->pending_ready_idx = n + p->settle;
        }
      }
    } else if (yb > p->peak_val) {
      p->peak_val = yb;
      p->peak_idx = n;
    } else if (yb < p->peak_val - thr) {
      const bool outside_refractory =
          !p->have_peak || p->peak_idx - p->last_peak_idx >= p->refractory;
      if (outside_refractory) {
        if (p->pending && !emitted) {
          finalize(p, (int64_t)n, beat);
          p->pending = false;
          emitted = true;
        }
        if (!p->pending) {
          const float swing = p->peak_val - p->trough_val;
          p->pending = true;
          p->pending_have_next = false;
          p->pending_trough_idx = p->trough_idx;
          p->pending_peak_idx = p->peak_idx;
          p->pending_ac = swing;
          p->pending_dc = p->dc;
          p->amp_ema = 0.8f * p->amp_ema + 0.2f * swing;
          p->have_peak = true;
          p->last_peak_idx = p->peak_idx;
        }
      }
      p->seeking_peak = false;
      p->trough_val = yb;
      p->trough_idx = n;
    }

    if (n - p->last_peak_idx > seconds_to_samples(p->fs, 1.5f)) {
      p->amp_ema *= 0.99f;
    }
  }

  p->n = n + 1;
  return emitted;
}
