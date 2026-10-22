#include "dsp_pipeline.h"

#include <string.h>

#define RATE_MIN_INTERVAL_US 300000UL    // 200 bpm
#define RATE_MAX_INTERVAL_US 2000000UL   // 30 bpm
#define SPO2_PAIR_WINDOW_US 150000UL

void dsp_pipeline_init(dsp_pipeline_t *d, const dsp_config_t *cfg)
{
  memset(d, 0, sizeof(*d));
  d->cfg = *cfg;

  ecg_filter_config_t ecg_cfg = cfg->ecg_filter;
  ecg_cfg.fs = cfg->ecg_fs;
  ecg_filter_init(&d->ecg_filter, &ecg_cfg);

  pan_tompkins_config_t pt_cfg = {
      .fs = cfg->ecg_fs,
      .input_delay_samples = ecg_filter_delay_samples(&d->ecg_filter),
      .learning_s = 2.0f,
  };
  pan_tompkins_init(&d->pt, &pt_cfg);

  ppg_processor_config_t ppg_cfg = {
      .fs = cfg->ppg_fs,
      .dc_lpf_hz = cfg->ppg_dc_lpf_hz,
      .bp_lo_hz = cfg->ppg_bp_lo_hz,
      .bp_hi_hz = cfg->ppg_bp_hi_hz,
      .timing_lpf_hz = cfg->ppg_timing_lpf_hz,
      .delay_ref_hz = cfg->ppg_delay_ref_hz,
      .learning_s = 2.0f,
  };
  ppg_processor_init(&d->ppg_red, &ppg_cfg);
  ppg_processor_init(&d->ppg_ir, &ppg_cfg);

  spo2_init(&d->spo2, cfg->spo2_a, cfg->spo2_b, cfg->spo2_c, SPO2_PAIR_WINDOW_US);
  pat_init(&d->pat, cfg->pat_min_us, cfg->pat_max_us);
  rate_tracker_init(&d->hr, RATE_MIN_INTERVAL_US, RATE_MAX_INTERVAL_US);
  rate_tracker_init(&d->pr, RATE_MIN_INTERVAL_US, RATE_MAX_INTERVAL_US);

  d->ecg_contact = true;
}

static uint32_t latest_time(const dsp_pipeline_t *d)
{
  return (int32_t)(d->last_ecg_t_us - d->last_ppg_t_us) >= 0 ? d->last_ecg_t_us
                                                             : d->last_ppg_t_us;
}

static bool warmed_up(const dsp_pipeline_t *d, uint32_t since_us, uint16_t beats, uint32_t now)
{
  return (int32_t)(now - since_us) >= (int32_t)d->cfg.warmup_us && beats >= d->cfg.warmup_beats;
}

static uint8_t current_flags(const dsp_pipeline_t *d, uint32_t now)
{
  uint8_t flags = 0;
  float unused;
  const bool ecg_ready =
      d->ecg_contact && warmed_up(d, d->ecg_contact_since_us, d->ecg_beats_since_contact, now);
  const bool ppg_ready =
      d->finger && warmed_up(d, d->finger_since_us, d->ppg_beats_since_finger, now);

  if (d->ecg_contact) flags |= VITALS_FLAG_ECG_CONTACT;
  if (d->finger) flags |= VITALS_FLAG_PPG_FINGER;
  if (ecg_ready && rate_tracker_bpm(&d->hr, &unused)) flags |= VITALS_FLAG_HR_VALID;
  if (ppg_ready && rate_tracker_bpm(&d->pr, &unused)) flags |= VITALS_FLAG_PR_VALID;
  if (ppg_ready && spo2_get(&d->spo2, NULL, NULL)) flags |= VITALS_FLAG_SPO2_VALID;
  if (ecg_ready && ppg_ready && d->have_pat &&
      (uint32_t)(now - d->last_pat_t_us) < DSP_PAT_STALE_US) {
    flags |= VITALS_FLAG_PAT_VALID;
  }
  return flags;
}

static void enqueue_beat(dsp_pipeline_t *d, const beat_event_t *e)
{
  const uint8_t next = (uint8_t)((d->beat_head + 1) % DSP_BEAT_QUEUE);
  if (next == d->beat_tail) {
    return;
  }
  d->beat_q[d->beat_head] = *e;
  d->beat_head = next;
}

// Emits the oldest `count` R-peaks still waiting for a PPG match.
static void release_awaiting(dsp_pipeline_t *d, uint8_t count)
{
  if (count > d->await_count) {
    count = d->await_count;
  }
  for (uint8_t i = 0; i < count; i++) {
    enqueue_beat(d, &d->await[i]);
  }
  memmove(&d->await[0], &d->await[count], (d->await_count - count) * sizeof(beat_event_t));
  d->await_count -= count;
}

static void release_stale(dsp_pipeline_t *d, uint32_t now)
{
  uint8_t stale = 0;
  while (stale < d->await_count &&
         (uint32_t)(now - d->await[stale].r_peak_t_us) > DSP_PAT_WAIT_US) {
    stale++;
  }
  release_awaiting(d, stale);
}

void dsp_pipeline_set_ecg_contact(dsp_pipeline_t *d, bool contact, uint32_t t_us)
{
  if (contact == d->ecg_contact) {
    return;
  }
  d->ecg_contact = contact;
  if (contact) {
    d->ecg_contact_since_us = t_us;
    d->ecg_beats_since_contact = 0;
    pan_tompkins_reset(&d->pt);
  } else {
    release_awaiting(d, d->await_count);
    rate_tracker_reset(&d->hr);
    pat_reset(&d->pat);
    d->have_pat = false;
  }
}

void dsp_pipeline_push_ecg(dsp_pipeline_t *d, const sample_t *ecg)
{
  if (d->last_ecg_t_us == 0 && d->ecg_contact_since_us == 0) {
    d->ecg_contact_since_us = ecg->timestamp_us;
  }
  d->last_ecg_t_us = ecg->timestamp_us;
  d->ecg_filtered = ecg_filter_process(&d->ecg_filter, (float)ecg->raw_sample);
  release_stale(d, ecg->timestamp_us);

  pt_beat_t beat;
  if (!pan_tompkins_process(&d->pt, d->ecg_filtered, ecg->timestamp_us, &beat) ||
      !d->ecg_contact) {
    return;
  }

  rate_tracker_add(&d->hr, beat.t_us);
  if (d->ecg_beats_since_contact < UINT16_MAX) {
    d->ecg_beats_since_contact++;
  }
  pat_add_r_peak(&d->pat, beat.t_us);

  if (d->await_count == DSP_AWAIT_MAX) {
    release_awaiting(d, 1);
  }
  beat_event_t *e = &d->await[d->await_count++];
  e->r_peak_t_us = beat.t_us;
  e->rr_us = d->hr.last_interval_us;
  e->pat_foot_us = 0;
  e->pat_peak_us = 0;
  e->flags = current_flags(d, latest_time(d)) & (uint8_t)~VITALS_FLAG_PAT_VALID;
}

static void update_finger(dsp_pipeline_t *d, uint32_t t_us)
{
  const bool finger = ppg_processor_dc(&d->ppg_ir) > d->cfg.ppg_finger_dc_min;
  if (finger == d->finger) {
    return;
  }
  d->finger = finger;
  rate_tracker_reset(&d->pr);
  spo2_reset(&d->spo2);
  d->have_pat = false;
  if (finger) {
    d->finger_since_us = t_us;
    d->ppg_beats_since_finger = 0;
    ppg_processor_reset(&d->ppg_red);
    ppg_processor_reset(&d->ppg_ir);
  }
}

static void handle_ir_beat(dsp_pipeline_t *d, const ppg_beat_t *ir)
{
  spo2_add_ir(&d->spo2, ir);
  rate_tracker_add(&d->pr, ir->t_peak_us);
  if (d->ppg_beats_since_finger < UINT16_MAX) {
    d->ppg_beats_since_finger++;
  }
  if (!d->ecg_contact) {
    return;
  }

  pat_result_t res;
  if (!pat_add_ppg_beat(&d->pat, ir, &res)) {
    return;
  }
  if (res.foot_valid) {
    d->have_pat = true;
    d->last_pat_t_us = ir->t_foot_us;
  }
  for (uint8_t i = 0; i < d->await_count; i++) {
    beat_event_t *e = &d->await[i];
    if (e->r_peak_t_us != res.r_peak_t_us) {
      continue;
    }
    e->pat_foot_us = res.pat_foot_us;
    e->pat_peak_us = res.pat_peak_us;
    if (res.foot_valid && res.peak_valid) {
      e->flags |= VITALS_FLAG_PAT_VALID;
    }
    release_awaiting(d, (uint8_t)(i + 1));
    break;
  }
}

void dsp_pipeline_push_ppg(dsp_pipeline_t *d, const sample_t *red, const sample_t *ir)
{
  d->last_ppg_t_us = ir->timestamp_us;

  ppg_beat_t red_beat, ir_beat;
  const bool have_red =
      ppg_processor_process(&d->ppg_red, (float)red->raw_sample, red->timestamp_us, &red_beat);
  const bool have_ir =
      ppg_processor_process(&d->ppg_ir, (float)ir->raw_sample, ir->timestamp_us, &ir_beat);

  update_finger(d, ir->timestamp_us);
  if (!d->finger) {
    return;
  }
  if (have_red) {
    spo2_add_red(&d->spo2, &red_beat);
  }
  if (have_ir) {
    handle_ir_beat(d, &ir_beat);
  }
}

bool dsp_pipeline_pop_beat(dsp_pipeline_t *d, beat_event_t *out)
{
  if (d->beat_tail == d->beat_head) {
    return false;
  }
  *out = d->beat_q[d->beat_tail];
  d->beat_tail = (uint8_t)((d->beat_tail + 1) % DSP_BEAT_QUEUE);
  return true;
}

void dsp_pipeline_get_vitals(const dsp_pipeline_t *d, vitals_t *out)
{
  memset(out, 0, sizeof(*out));
  out->timestamp_us = latest_time(d);
  out->flags = current_flags(d, out->timestamp_us);

  if (out->flags & VITALS_FLAG_HR_VALID) {
    rate_tracker_bpm(&d->hr, &out->hr_bpm);
  }
  if (out->flags & VITALS_FLAG_PR_VALID) {
    rate_tracker_bpm(&d->pr, &out->pr_bpm);
  }
  if (out->flags & VITALS_FLAG_SPO2_VALID) {
    spo2_get(&d->spo2, &out->spo2_pct, &out->r_ratio);
  }
  if (out->flags & VITALS_FLAG_PAT_VALID) {
    pat_median(&d->pat, &out->pat_foot_us, &out->pat_peak_us);
  }
}

float dsp_pipeline_ecg_filtered(const dsp_pipeline_t *d)
{
  return d->ecg_filtered;
}
