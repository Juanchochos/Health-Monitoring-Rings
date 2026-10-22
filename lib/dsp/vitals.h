#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VITALS_FLAG_ECG_CONTACT  0x01
#define VITALS_FLAG_PPG_FINGER   0x02
#define VITALS_FLAG_HR_VALID     0x04
#define VITALS_FLAG_PR_VALID     0x08
#define VITALS_FLAG_SPO2_VALID   0x10
#define VITALS_FLAG_PAT_VALID    0x20

typedef struct {
  uint32_t timestamp_us;
  float hr_bpm;
  float pr_bpm;
  float spo2_pct;
  float r_ratio;
  uint32_t pat_foot_us;      // median of recent beats
  uint32_t pat_peak_us;
  uint8_t flags;
} vitals_t;

// One event per detected R-peak, carrying its PAT when a PPG beat matched.
typedef struct {
  uint32_t r_peak_t_us;
  uint32_t rr_us;            // 0 if no valid preceding interval
  uint32_t pat_foot_us;
  uint32_t pat_peak_us;
  uint8_t flags;             // VITALS_FLAG_* snapshot; PAT_VALID if PAT fields are set
} beat_event_t;

// Beat-to-beat rate from event times: median of the last RATE_HISTORY
// intervals inside [min_interval_us, max_interval_us].
#define RATE_HISTORY 8

typedef struct {
  uint32_t min_interval_us;
  uint32_t max_interval_us;
  uint32_t last_t_us;
  bool have_last;
  float intervals[RATE_HISTORY];
  uint8_t count;
  uint8_t pos;
  uint32_t last_interval_us;
} rate_tracker_t;

void rate_tracker_init(rate_tracker_t *rt, uint32_t min_interval_us, uint32_t max_interval_us);
void rate_tracker_reset(rate_tracker_t *rt);

// Returns true if the event produced an in-range interval. Events closer than
// min_interval_us to the previous one are ignored entirely.
bool rate_tracker_add(rate_tracker_t *rt, uint32_t t_us);

// Returns false until at least two intervals are available.
bool rate_tracker_bpm(const rate_tracker_t *rt, float *bpm);

#ifdef __cplusplus
}
#endif
