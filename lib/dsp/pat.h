#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ppg_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pulse arrival time: delay from the ECG R-peak to the PPG foot and systolic
// peak of the same heartbeat. Each PPG beat is paired with the latest unused
// R-peak whose foot delay lies in [min_us, max_us]. R-peaks are always
// confirmed before the PPG beat (PPG fiducials are finalized ~300 ms after
// the systolic peak), so no PPG beats need to be held back.
//
// A value further than 3 MAD (floored at mad_floor_us) from the median of the
// last PAT_HISTORY values is flagged as an outlier; it still enters the
// history so a genuine step change is accepted after a few beats.

#define PAT_R_HISTORY 4
#define PAT_HISTORY 8
#define PAT_MIN_FOR_OUTLIER 5

typedef struct {
  uint32_t r_peak_t_us;
  uint32_t pat_foot_us;
  uint32_t pat_peak_us;
  bool foot_valid;           // false when rejected as an outlier
  bool peak_valid;
} pat_result_t;

typedef struct {
  uint32_t min_us;
  uint32_t max_us;
  float mad_floor_us;
  uint32_t r_t[PAT_R_HISTORY];
  bool r_used[PAT_R_HISTORY];
  uint8_t r_count;
  uint8_t r_pos;
  float foot_hist[PAT_HISTORY];
  float peak_hist[PAT_HISTORY];
  uint8_t hist_count;
  uint8_t hist_pos;
} pat_t;

void pat_init(pat_t *p, uint32_t min_us, uint32_t max_us);
void pat_reset(pat_t *p);

void pat_add_r_peak(pat_t *p, uint32_t t_us);

// Returns true when the beat matched an R-peak.
bool pat_add_ppg_beat(pat_t *p, const ppg_beat_t *beat, pat_result_t *out);

// Median PAT over the recent history; false if empty.
bool pat_median(const pat_t *p, uint32_t *foot_us, uint32_t *peak_us);

#ifdef __cplusplus
}
#endif
