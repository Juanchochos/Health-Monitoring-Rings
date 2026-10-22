#pragma once

#include <stdbool.h>

#include "biquad.h"

#ifdef __cplusplus
extern "C" {
#endif

// ECG conditioning: 2nd-order Butterworth high-pass, 4th-order Butterworth
// low-pass, and an optional powerline notch.
typedef struct {
  float fs;
  bool bandpass_enable;
  float hpf_hz;
  float lpf_hz;
  bool notch_enable;
  float notch_hz;
  float notch_q;
  float delay_ref_hz;   // frequency at which group delay is reported
} ecg_filter_config_t;

typedef struct {
  biquad_cascade_t chain;
  float fs;
  float delay_samples;
  bool primed;
} ecg_filter_t;

bool ecg_filter_init(ecg_filter_t *f, const ecg_filter_config_t *cfg);

// The first sample primes the filter state to avoid a start-up transient.
float ecg_filter_process(ecg_filter_t *f, float x);

float ecg_filter_delay_samples(const ecg_filter_t *f);
const biquad_cascade_t *ecg_filter_chain(const ecg_filter_t *f);

#ifdef __cplusplus
}
#endif
