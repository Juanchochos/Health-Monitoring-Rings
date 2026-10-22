#include "pat.h"

#include <math.h>

#include "dsp_util.h"

void pat_init(pat_t *p, uint32_t min_us, uint32_t max_us)
{
  p->min_us = min_us;
  p->max_us = max_us;
  p->mad_floor_us = 2000.0f;
  pat_reset(p);
}

void pat_reset(pat_t *p)
{
  p->r_count = 0;
  p->r_pos = 0;
  p->hist_count = 0;
  p->hist_pos = 0;
}

void pat_add_r_peak(pat_t *p, uint32_t t_us)
{
  p->r_t[p->r_pos] = t_us;
  p->r_used[p->r_pos] = false;
  p->r_pos = (uint8_t)((p->r_pos + 1) % PAT_R_HISTORY);
  if (p->r_count < PAT_R_HISTORY) {
    p->r_count++;
  }
}

static bool is_inlier(const pat_t *p, const float *hist, float value)
{
  if (p->hist_count < PAT_MIN_FOR_OUTLIER) {
    return true;
  }
  const float median = dsp_median(hist, p->hist_count);
  const float mad = fmaxf(dsp_mad(hist, p->hist_count, median), p->mad_floor_us);
  return fabsf(value - median) <= 3.0f * mad;
}

bool pat_add_ppg_beat(pat_t *p, const ppg_beat_t *beat, pat_result_t *out)
{
  int8_t match = -1;
  uint32_t best_delay = 0;
  for (uint8_t i = 0; i < p->r_count; i++) {
    if (p->r_used[i]) {
      continue;
    }
    const int32_t delay = (int32_t)(beat->t_foot_us - p->r_t[i]);
    if (delay < (int32_t)p->min_us || delay > (int32_t)p->max_us) {
      continue;
    }
    if (match < 0 || (uint32_t)delay < best_delay) {
      match = (int8_t)i;
      best_delay = (uint32_t)delay;
    }
  }
  if (match < 0) {
    return false;
  }
  p->r_used[match] = true;

  const uint32_t r_t = p->r_t[match];
  out->r_peak_t_us = r_t;
  out->pat_foot_us = beat->t_foot_us - r_t;
  out->pat_peak_us = beat->t_peak_us - r_t;
  out->foot_valid = is_inlier(p, p->foot_hist, (float)out->pat_foot_us);
  out->peak_valid = is_inlier(p, p->peak_hist, (float)out->pat_peak_us);

  p->foot_hist[p->hist_pos] = (float)out->pat_foot_us;
  p->peak_hist[p->hist_pos] = (float)out->pat_peak_us;
  p->hist_pos = (uint8_t)((p->hist_pos + 1) % PAT_HISTORY);
  if (p->hist_count < PAT_HISTORY) {
    p->hist_count++;
  }
  return true;
}

bool pat_median(const pat_t *p, uint32_t *foot_us, uint32_t *peak_us)
{
  if (p->hist_count == 0) {
    return false;
  }
  *foot_us = (uint32_t)lroundf(dsp_median(p->foot_hist, p->hist_count));
  *peak_us = (uint32_t)lroundf(dsp_median(p->peak_hist, p->hist_count));
  return true;
}
