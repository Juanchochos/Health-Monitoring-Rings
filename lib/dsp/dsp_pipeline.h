#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecg_filter.h"
#include "pan_tompkins.h"
#include "pat.h"
#include "ppg_processor.h"
#include "sample.h"
#include "spo2.h"
#include "vitals.h"

#ifdef __cplusplus
extern "C" {
#endif

// The complete DSP/biometrics layer. Feed timestamped samples in time order;
// read back the latest vitals and one beat event per R-peak.

typedef struct {
  float ecg_fs;
  ecg_filter_config_t ecg_filter;
  float ppg_fs;
  float ppg_dc_lpf_hz;
  float ppg_bp_lo_hz;
  float ppg_bp_hi_hz;
  float ppg_timing_lpf_hz;
  float ppg_delay_ref_hz;
  float ppg_finger_dc_min;
  uint32_t pat_min_us;
  uint32_t pat_max_us;
  float spo2_a;
  float spo2_b;
  float spo2_c;
  uint32_t warmup_us;
  uint8_t warmup_beats;
} dsp_config_t;

#define DSP_BEAT_QUEUE 8
#define DSP_PAT_STALE_US 5000000UL
// R-peaks wait this long for their PPG beat (which is final only after the
// next pulse's upstroke) before being emitted without PAT.
#define DSP_PAT_WAIT_US 3000000UL
#define DSP_AWAIT_MAX PAT_R_HISTORY

typedef struct {
  dsp_config_t cfg;

  ecg_filter_t ecg_filter;
  pan_tompkins_t pt;
  ppg_processor_t ppg_red;
  ppg_processor_t ppg_ir;
  spo2_t spo2;
  pat_t pat;
  rate_tracker_t hr;
  rate_tracker_t pr;

  bool ecg_contact;
  uint32_t ecg_contact_since_us;
  uint16_t ecg_beats_since_contact;
  bool finger;
  uint32_t finger_since_us;
  uint16_t ppg_beats_since_finger;

  uint32_t last_ecg_t_us;
  uint32_t last_ppg_t_us;
  uint32_t last_pat_t_us;
  bool have_pat;

  beat_event_t await[DSP_AWAIT_MAX];   // oldest first
  uint8_t await_count;
  beat_event_t beat_q[DSP_BEAT_QUEUE];
  uint8_t beat_head;
  uint8_t beat_tail;

  float ecg_filtered;
} dsp_pipeline_t;

void dsp_pipeline_init(dsp_pipeline_t *d, const dsp_config_t *cfg);

// Electrode contact from the ECG AFE lead-off status.
void dsp_pipeline_set_ecg_contact(dsp_pipeline_t *d, bool contact, uint32_t t_us);

void dsp_pipeline_push_ecg(dsp_pipeline_t *d, const sample_t *ecg);
void dsp_pipeline_push_ppg(dsp_pipeline_t *d, const sample_t *red, const sample_t *ir);

bool dsp_pipeline_pop_beat(dsp_pipeline_t *d, beat_event_t *out);
void dsp_pipeline_get_vitals(const dsp_pipeline_t *d, vitals_t *out);

// Latest conditioned ECG value (for waveform streaming).
float dsp_pipeline_ecg_filtered(const dsp_pipeline_t *d);

#ifdef __cplusplus
}
#endif
