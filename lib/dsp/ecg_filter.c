#include "ecg_filter.h"

bool ecg_filter_init(ecg_filter_t *f, const ecg_filter_config_t *cfg)
{
  biquad_cascade_init(&f->chain);
  f->fs = cfg->fs;
  f->primed = false;

  bool ok = true;
  if (cfg->bandpass_enable) {
    ok &= biquad_cascade_add_butter_highpass(&f->chain, cfg->fs, cfg->hpf_hz, 2);
    ok &= biquad_cascade_add_butter_lowpass(&f->chain, cfg->fs, cfg->lpf_hz, 4);
  }
  if (cfg->notch_enable && cfg->notch_hz < 0.5f * cfg->fs) {
    biquad_t notch;
    biquad_design_notch(&notch, cfg->fs, cfg->notch_hz, cfg->notch_q);
    ok &= biquad_cascade_add(&f->chain, &notch);
  }
  f->delay_samples = biquad_cascade_group_delay(&f->chain, cfg->fs, cfg->delay_ref_hz);
  return ok;
}

float ecg_filter_process(ecg_filter_t *f, float x)
{
  if (!f->primed) {
    biquad_cascade_prime(&f->chain, x);
    f->primed = true;
  }
  return biquad_cascade_process(&f->chain, x);
}

float ecg_filter_delay_samples(const ecg_filter_t *f)
{
  return f->delay_samples;
}

const biquad_cascade_t *ecg_filter_chain(const ecg_filter_t *f)
{
  return &f->chain;
}
