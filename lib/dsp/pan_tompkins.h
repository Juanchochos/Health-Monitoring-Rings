#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "biquad.h"

#ifdef __cplusplus
extern "C" {
#endif

// Streaming Pan-Tompkins QRS detector, parameterized by sample rate.
//
// Detection runs on: 5-15 Hz bandpass -> 5-point derivative -> squaring ->
// 150 ms moving-window integration (MWI). MWI humps are classified with the
// adaptive SPKI/NPKI thresholds, a 200 ms refractory period, slope-based
// T-wave rejection inside 360 ms, and searchback at 1.66x the mean RR.
//
// Each detection is then refined on the conditioned ECG that was fed in: the
// largest sample (by learned polarity) inside the delay-compensated MWI window
// +/-50 ms, with parabolic sub-sample interpolation. The reported time
// subtracts input_delay_samples, the group delay of the conditioning filter.

#define PT_MWI_MAX 64
#define PT_HISTORY 256
#define PT_RR_COUNT 8

typedef struct {
  float fs;
  float input_delay_samples;
  float learning_s;          // initial threshold-learning period (Pan-Tompkins: 2 s)
} pan_tompkins_config_t;

typedef struct {
  uint32_t t_us;
  float amplitude;           // conditioned ECG value at the R-peak
  bool searchback;
} pt_beat_t;

typedef struct {
  float fs;
  float ts_us;
  float input_delay_samples;
  uint32_t learning_samples;

  biquad_cascade_t bp;
  float bp_delay;
  bool primed;
  float bp_hist[4];
  float sq[PT_MWI_MAX];
  uint16_t mwi_len;
  uint16_t sq_pos;

  uint32_t n;
  float hist_x[PT_HISTORY];
  uint32_t hist_t[PT_HISTORY];

  bool rising;
  float hump_max;
  uint32_t hump_idx;
  float hump_slope;
  float valley;

  bool learning;
  uint32_t learn_start;
  float learn_max;
  double learn_sum;
  float spki;
  float npki;

  bool have_qrs;
  uint32_t last_qrs_idx;
  float last_qrs_slope;
  uint32_t rr[PT_RR_COUNT];
  uint8_t rr_count;
  uint8_t rr_pos;
  float rr_avg;
  uint32_t refractory;
  uint32_t twave_window;
  uint32_t margin;

  bool cand_valid;
  float cand_value;
  float cand_slope;
  uint32_t cand_idx;
  pt_beat_t cand_beat;

  int8_t polarity;
  int8_t polarity_sum;
  uint8_t polarity_votes;
} pan_tompkins_t;

void pan_tompkins_init(pan_tompkins_t *pt, const pan_tompkins_config_t *cfg);

// Restart learning (e.g. after electrode contact is re-acquired).
void pan_tompkins_reset(pan_tompkins_t *pt);

// Feed one conditioned ECG sample. Returns true and fills *beat when a QRS is
// confirmed (typically 150-300 ms after the R-peak itself).
bool pan_tompkins_process(pan_tompkins_t *pt, float x, uint32_t t_us, pt_beat_t *beat);

#ifdef __cplusplus
}
#endif
