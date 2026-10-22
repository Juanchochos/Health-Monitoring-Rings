#include "vitals.h"

#include "dsp_util.h"

void rate_tracker_init(rate_tracker_t *rt, uint32_t min_interval_us, uint32_t max_interval_us)
{
  rt->min_interval_us = min_interval_us;
  rt->max_interval_us = max_interval_us;
  rate_tracker_reset(rt);
}

void rate_tracker_reset(rate_tracker_t *rt)
{
  rt->have_last = false;
  rt->count = 0;
  rt->pos = 0;
  rt->last_interval_us = 0;
}

bool rate_tracker_add(rate_tracker_t *rt, uint32_t t_us)
{
  bool valid = false;
  rt->last_interval_us = 0;
  if (rt->have_last) {
    const uint32_t interval = t_us - rt->last_t_us;
    if (interval < rt->min_interval_us) {
      return false;  // double detection: keep the earlier event as reference
    }
    if (interval <= rt->max_interval_us) {
      rt->intervals[rt->pos] = (float)interval;
      rt->pos = (uint8_t)((rt->pos + 1) % RATE_HISTORY);
      if (rt->count < RATE_HISTORY) {
        rt->count++;
      }
      rt->last_interval_us = interval;
      valid = true;
    }
  }
  rt->last_t_us = t_us;
  rt->have_last = true;
  return valid;
}

bool rate_tracker_bpm(const rate_tracker_t *rt, float *bpm)
{
  if (rt->count < 2) {
    return false;
  }
  *bpm = 60e6f / dsp_median(rt->intervals, rt->count);
  return true;
}
