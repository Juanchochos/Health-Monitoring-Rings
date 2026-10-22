#include "pan_tompkins.h"

#include <math.h>
#include <string.h>

#define PT_HIST_MASK (PT_HISTORY - 1)

static uint32_t seconds_to_samples(float fs, float s)
{
  return (uint32_t)lroundf(fs * s);
}

void pan_tompkins_init(pan_tompkins_t *pt, const pan_tompkins_config_t *cfg)
{
  memset(pt, 0, sizeof(*pt));
  pt->fs = cfg->fs;
  pt->ts_us = 1e6f / cfg->fs;
  pt->input_delay_samples = cfg->input_delay_samples;
  pt->learning_samples = seconds_to_samples(cfg->fs, cfg->learning_s > 0 ? cfg->learning_s : 2.0f);

  biquad_cascade_init(&pt->bp);
  biquad_cascade_add_butter_highpass(&pt->bp, cfg->fs, 5.0f, 2);
  biquad_cascade_add_butter_lowpass(&pt->bp, cfg->fs, 15.0f, 2);
  pt->bp_delay = biquad_cascade_group_delay(&pt->bp, cfg->fs, 10.0f);

  pt->mwi_len = (uint16_t)seconds_to_samples(cfg->fs, 0.150f);
  if (pt->mwi_len > PT_MWI_MAX) {
    pt->mwi_len = PT_MWI_MAX;
  }
  if (pt->mwi_len < 1) {
    pt->mwi_len = 1;
  }
  pt->refractory = seconds_to_samples(cfg->fs, 0.200f);
  pt->twave_window = seconds_to_samples(cfg->fs, 0.360f);
  pt->margin = seconds_to_samples(cfg->fs, 0.050f);

  pan_tompkins_reset(pt);
}

void pan_tompkins_reset(pan_tompkins_t *pt)
{
  pt->primed = false;
  memset(pt->bp_hist, 0, sizeof(pt->bp_hist));
  memset(pt->sq, 0, sizeof(pt->sq));
  pt->sq_pos = 0;
  pt->rising = false;
  pt->hump_max = 0.0f;
  pt->hump_slope = 0.0f;
  pt->valley = 0.0f;
  pt->learning = true;
  pt->learn_max = 0.0f;
  pt->learn_sum = 0.0;
  pt->spki = 0.0f;
  pt->npki = 0.0f;
  pt->have_qrs = false;
  pt->rr_count = 0;
  pt->rr_pos = 0;
  pt->rr_avg = 0.0f;
  pt->cand_valid = false;
  pt->polarity = 0;
  pt->polarity_sum = 0;
  pt->polarity_votes = 0;
  pt->learn_start = pt->n;
}

static float score(const pan_tompkins_t *pt, float x)
{
  return pt->polarity == 0 ? fabsf(x) : (float)pt->polarity * x;
}

// Locate the R-peak on the conditioned ECG for an MWI hump peaking at p.
static pt_beat_t refine(const pan_tompkins_t *pt, uint32_t p)
{
  const int64_t now = (int64_t)pt->n;
  const int64_t oldest = now - PT_HISTORY + 1;
  const int64_t lag = (int64_t)lroundf(pt->bp_delay + 2.0f);
  int64_t hi = (int64_t)p - lag + pt->margin;
  int64_t lo = (int64_t)p - lag - (pt->mwi_len - 1) - pt->margin;
  if (hi > now) hi = now;
  if (lo < oldest) lo = oldest;
  if (lo < 0) lo = 0;
  if (hi < lo) hi = lo;

  int64_t best = lo;
  float best_score = score(pt, pt->hist_x[lo & PT_HIST_MASK]);
  for (int64_t i = lo + 1; i <= hi; i++) {
    float s = score(pt, pt->hist_x[i & PT_HIST_MASK]);
    if (s > best_score) {
      best_score = s;
      best = i;
    }
  }

  float frac = 0.0f;
  if (best - 1 >= oldest && best - 1 >= 0 && best + 1 <= now) {
    const float ym1 = score(pt, pt->hist_x[(best - 1) & PT_HIST_MASK]);
    const float yp1 = score(pt, pt->hist_x[(best + 1) & PT_HIST_MASK]);
    const float den = ym1 - 2.0f * best_score + yp1;
    if (den < 0.0f) {
      frac = 0.5f * (ym1 - yp1) / den;
      if (frac > 0.5f) frac = 0.5f;
      if (frac < -0.5f) frac = -0.5f;
    }
  }

  pt_beat_t beat;
  const float offset_us = (frac - pt->input_delay_samples) * pt->ts_us;
  beat.t_us = pt->hist_t[best & PT_HIST_MASK] + (uint32_t)(int32_t)lrintf(offset_us);
  beat.amplitude = pt->hist_x[best & PT_HIST_MASK];
  beat.searchback = false;
  return beat;
}

static void update_rr(pan_tompkins_t *pt, uint32_t p)
{
  if (!pt->have_qrs) {
    return;
  }
  const uint32_t rr = p - pt->last_qrs_idx;
  if (rr < seconds_to_samples(pt->fs, 0.25f) || rr > seconds_to_samples(pt->fs, 2.5f)) {
    return;
  }
  pt->rr[pt->rr_pos] = rr;
  pt->rr_pos = (uint8_t)((pt->rr_pos + 1) % PT_RR_COUNT);
  if (pt->rr_count < PT_RR_COUNT) {
    pt->rr_count++;
  }
  float sum = 0.0f;
  for (uint8_t i = 0; i < pt->rr_count; i++) {
    sum += (float)pt->rr[i];
  }
  pt->rr_avg = sum / pt->rr_count;
}

static void accept(pan_tompkins_t *pt, uint32_t p, float slope, pt_beat_t beat, bool searchback,
                   pt_beat_t *out)
{
  update_rr(pt, p);
  pt->have_qrs = true;
  pt->last_qrs_idx = p;
  pt->last_qrs_slope = slope;
  pt->cand_valid = false;

  if (pt->polarity == 0) {
    pt->polarity_sum += beat.amplitude >= 0.0f ? 1 : -1;
    if (++pt->polarity_votes >= 4) {
      pt->polarity = pt->polarity_sum >= 0 ? 1 : -1;
    }
  }
  beat.searchback = searchback;
  *out = beat;
}

static float thr1(const pan_tompkins_t *pt)
{
  return pt->npki + 0.25f * (pt->spki - pt->npki);
}

static bool classify(pan_tompkins_t *pt, float v, uint32_t p, float slope, pt_beat_t *out)
{
  if (pt->have_qrs && p - pt->last_qrs_idx < pt->refractory) {
    return false;
  }
  const pt_beat_t beat = refine(pt, p);
  const float t1 = thr1(pt);

  if (v > t1) {
    const bool t_wave = pt->have_qrs && (p - pt->last_qrs_idx) < pt->twave_window &&
                        slope < 0.5f * pt->last_qrs_slope;
    if (t_wave) {
      pt->npki = 0.125f * v + 0.875f * pt->npki;
      return false;
    }
    pt->spki = 0.125f * v + 0.875f * pt->spki;
    accept(pt, p, slope, beat, false, out);
    return true;
  }

  pt->npki = 0.125f * v + 0.875f * pt->npki;
  if (v > 0.5f * t1 && (!pt->cand_valid || v > pt->cand_value)) {
    pt->cand_valid = true;
    pt->cand_value = v;
    pt->cand_slope = slope;
    pt->cand_idx = p;
    pt->cand_beat = beat;
  }
  return false;
}

bool pan_tompkins_process(pan_tompkins_t *pt, float x, uint32_t t_us, pt_beat_t *beat)
{
  const uint32_t n = pt->n;
  pt->hist_x[n & PT_HIST_MASK] = x;
  pt->hist_t[n & PT_HIST_MASK] = t_us;

  if (!pt->primed) {
    biquad_cascade_prime(&pt->bp, x);
    pt->primed = true;
  }
  const float y = biquad_cascade_process(&pt->bp, x);
  const float d = (2.0f * y + pt->bp_hist[0] - pt->bp_hist[2] - 2.0f * pt->bp_hist[3]) * 0.125f;
  pt->bp_hist[3] = pt->bp_hist[2];
  pt->bp_hist[2] = pt->bp_hist[1];
  pt->bp_hist[1] = pt->bp_hist[0];
  pt->bp_hist[0] = y;

  pt->sq[pt->sq_pos] = d * d;
  pt->sq_pos = (uint16_t)((pt->sq_pos + 1) % pt->mwi_len);
  float sum = 0.0f;
  for (uint16_t i = 0; i < pt->mwi_len; i++) {
    sum += pt->sq[i];
  }
  const float m = sum / pt->mwi_len;
  const float slope = fabsf(d);

  bool detected = false;

  if (pt->learning) {
    if (m > pt->learn_max) {
      pt->learn_max = m;
    }
    pt->learn_sum += m;
    if (n - pt->learn_start + 1 >= pt->learning_samples) {
      pt->spki = pt->learn_max / 3.0f;
      pt->npki = (float)(pt->learn_sum / pt->learning_samples) / 2.0f;
      pt->learning = false;
    }
  }

  if (pt->rising) {
    if (m > pt->hump_max) {
      pt->hump_max = m;
      pt->hump_idx = n;
    }
    if (slope > pt->hump_slope) {
      pt->hump_slope = slope;
    }
    if (m < 0.5f * pt->hump_max) {
      pt->rising = false;
      pt->valley = m;
      if (!pt->learning) {
        detected = classify(pt, pt->hump_max, pt->hump_idx, pt->hump_slope, beat);
      }
    }
  } else if (m < pt->valley) {
    pt->valley = m;
  } else if (m > pt->valley) {
    pt->rising = true;
    pt->hump_max = m;
    pt->hump_idx = n;
    pt->hump_slope = slope;
  }

  if (!detected && !pt->learning && pt->have_qrs && pt->rr_count > 0 && pt->cand_valid &&
      (float)(n - pt->last_qrs_idx) > 1.66f * pt->rr_avg) {
    pt->spki = 0.25f * pt->cand_value + 0.75f * pt->spki;
    accept(pt, pt->cand_idx, pt->cand_slope, pt->cand_beat, true, beat);
    detected = true;
  }

  pt->n = n + 1;
  return detected;
}
