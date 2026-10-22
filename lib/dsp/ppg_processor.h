#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "biquad.h"

#ifdef __cplusplus
extern "C" {
#endif

// Single-wavelength PPG processing (one instance for Red, one for IR).
//
// Raw counts fall when blood volume rises, so the signal is inverted first.
//  - DC: 1st-order low-pass of the raw counts.
//  - AC: 0.5-5 Hz Butterworth bandpass; beats are detected on it with an
//    adaptive rise/fall threshold and a 300 ms refractory period, and the
//    beat's AC amplitude is its trough-to-peak swing.
//  - Fiducials are timed on a low-pass-only copy (no high-pass phase lead),
//    after removing the straight baseline through the beat's own foot minimum
//    and the next beat's foot minimum (cancels respiratory/drift tilt):
//    systolic peak = its maximum, foot = its steepest upslope (maximum first
//    derivative). Both use a 5-point least-squares parabola for sub-sample
//    interpolation and have the low-pass group delay removed.
//
// A beat is therefore final shortly after the next beat's upstroke starts
// (or after a timeout if no further beat arrives).

#define PPG_HISTORY 256            // >= 2.5 s at 100 Hz (down to 30 bpm)

typedef struct {
  float fs;
  float dc_lpf_hz;
  float bp_lo_hz;
  float bp_hi_hz;
  float timing_lpf_hz;       // low-pass for fiducial timing; <= 0 uses bp_hi_hz
  float delay_ref_hz;
  float learning_s;          // amplitude learning before detection starts
} ppg_processor_config_t;

typedef struct {
  uint32_t t_foot_us;
  uint32_t t_peak_us;
  float ac_pp;               // trough-to-peak of the bandpassed pulse
  float dc;
} ppg_beat_t;

typedef struct {
  float fs;
  float ts_us;
  float dc_alpha;
  float dc;
  bool primed;

  biquad_cascade_t bp;
  biquad_cascade_t lp;
  float lp_delay_samples;

  uint32_t n;
  float hist_lp[PPG_HISTORY];
  uint32_t hist_t[PPG_HISTORY];

  bool learning;
  uint32_t learn_len;
  uint32_t learn_end;
  float learn_min;
  float learn_max;

  bool seeking_peak;
  float trough_val;
  uint32_t trough_idx;
  float peak_val;
  uint32_t peak_idx;
  float amp_ema;
  bool have_peak;
  uint32_t last_peak_idx;
  uint32_t refractory;

  bool pending;
  bool pending_have_next;
  uint32_t pending_ready_idx;
  uint32_t pending_trough_idx;
  uint32_t pending_peak_idx;
  uint32_t pending_next_trough_idx;
  float pending_ac;
  float pending_dc;
  uint32_t settle;           // wait after the next trough is confirmed
  uint32_t timeout;          // finalize without detrend this long after the peak
} ppg_processor_t;

void ppg_processor_init(ppg_processor_t *p, const ppg_processor_config_t *cfg);
void ppg_processor_reset(ppg_processor_t *p);

// Feed one raw sample. Returns true and fills *beat when a beat's fiducials
// are final.
bool ppg_processor_process(ppg_processor_t *p, float raw, uint32_t t_us, ppg_beat_t *beat);

float ppg_processor_dc(const ppg_processor_t *p);

#ifdef __cplusplus
}
#endif
