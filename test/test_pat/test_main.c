#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unity.h>

#include "../common/synth.h"
#include "app_config.h"
#include "dsp_pipeline.h"
#include "dsp_util.h"
#include "pat.h"
#include "vitals.h"

void setUp(void) {}
void tearDown(void) {}

// ------------------------------------------------------------ helpers ----

static void test_median_and_mad(void)
{
  const float odd[] = {5, 1, 4, 2, 3};
  const float even[] = {4, 1, 3, 2};
  TEST_ASSERT_EQUAL_FLOAT(3.0f, dsp_median(odd, 5));
  TEST_ASSERT_EQUAL_FLOAT(2.5f, dsp_median(even, 4));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, dsp_mad(odd, 5, 3.0f));
}

static void test_rate_tracker(void)
{
  rate_tracker_t rt;
  rate_tracker_init(&rt, 300000u, 2000000u);
  float bpm;
  TEST_ASSERT_FALSE(rate_tracker_add(&rt, 1000000u));
  TEST_ASSERT_TRUE(rate_tracker_add(&rt, 1800000u));
  TEST_ASSERT_FALSE(rate_tracker_bpm(&rt, &bpm));
  TEST_ASSERT_TRUE(rate_tracker_add(&rt, 2600000u));
  TEST_ASSERT_TRUE(rate_tracker_bpm(&rt, &bpm));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f, bpm);

  // Out-of-range intervals (double detection, missed beat) are ignored.
  TEST_ASSERT_FALSE(rate_tracker_add(&rt, 2700000u));
  TEST_ASSERT_TRUE(rate_tracker_add(&rt, 3400000u));
  TEST_ASSERT_FALSE(rate_tracker_add(&rt, 6000000u));
  TEST_ASSERT_TRUE(rate_tracker_bpm(&rt, &bpm));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f, bpm);
}

static ppg_beat_t ppg_at(uint32_t foot_us, uint32_t peak_us)
{
  ppg_beat_t b = {foot_us, peak_us, 1000.0f, 100000.0f};
  return b;
}

static void test_pat_matches_latest_r_in_window(void)
{
  pat_t p;
  pat_init(&p, PAT_MIN_US, PAT_MAX_US);
  pat_add_r_peak(&p, 1000000u);
  pat_add_r_peak(&p, 1800000u);
  pat_result_t res;

  // Foot 200 ms after the second R (and 1 s after the first: out of window).
  ppg_beat_t b = ppg_at(2000000u, 2090000u);
  TEST_ASSERT_TRUE(pat_add_ppg_beat(&p, &b, &res));
  TEST_ASSERT_EQUAL_UINT32(1800000u, res.r_peak_t_us);
  TEST_ASSERT_EQUAL_UINT32(200000u, res.pat_foot_us);
  TEST_ASSERT_EQUAL_UINT32(290000u, res.pat_peak_us);
  TEST_ASSERT_TRUE(res.foot_valid && res.peak_valid);

  // The same R cannot be matched twice.
  b = ppg_at(2010000u, 2100000u);
  TEST_ASSERT_FALSE(pat_add_ppg_beat(&p, &b, &res));

  // Too soon after an R (< PAT_MIN_US) does not match.
  pat_add_r_peak(&p, 2600000u);
  b = ppg_at(2650000u, 2700000u);
  TEST_ASSERT_FALSE(pat_add_ppg_beat(&p, &b, &res));
}

static void test_pat_flags_outliers(void)
{
  pat_t p;
  pat_init(&p, PAT_MIN_US, PAT_MAX_US);
  pat_result_t res;
  uint32_t r = 1000000u;
  for (int k = 0; k < 8; k++, r += 800000u) {
    pat_add_r_peak(&p, r);
    const uint32_t jitter = (uint32_t)(k % 3) * 1000u;
    ppg_beat_t b = ppg_at(r + 200000u + jitter, r + 290000u + jitter);
    TEST_ASSERT_TRUE(pat_add_ppg_beat(&p, &b, &res));
    TEST_ASSERT_TRUE(res.foot_valid);
  }
  pat_add_r_peak(&p, r);
  ppg_beat_t b = ppg_at(r + 260000u, r + 350000u);
  TEST_ASSERT_TRUE(pat_add_ppg_beat(&p, &b, &res));
  TEST_ASSERT_FALSE(res.foot_valid);

  uint32_t foot, peak;
  TEST_ASSERT_TRUE(pat_median(&p, &foot, &peak));
  TEST_ASSERT_UINT32_WITHIN(2000u, 201000u, foot);
}

// ---------------------------------------------------- full pipeline ----

#define MAX_BEATS 256

typedef struct {
  double r[MAX_BEATS];        // R-wave centres (s)
  double ppg[MAX_BEATS];      // PPG systolic-peak centres (s)
  double foot[MAX_BEATS];     // true PPG fiducials (s)
  double peak[MAX_BEATS];
  int n;
} truth_t;

static dsp_config_t app_dsp_config(void)
{
  dsp_config_t c = {
      .ecg_fs = ECG_SAMPLE_RATE_HZ,
      .ecg_filter = {ECG_SAMPLE_RATE_HZ, ECG_SW_BANDPASS_ENABLE, ECG_HPF_HZ, ECG_LPF_HZ,
                     ECG_NOTCH_ENABLE, ECG_NOTCH_HZ, ECG_NOTCH_Q, ECG_DELAY_REF_HZ},
      .ppg_fs = PPG_SAMPLE_RATE_HZ,
      .ppg_dc_lpf_hz = PPG_DC_LPF_HZ,
      .ppg_bp_lo_hz = PPG_BP_LO_HZ,
      .ppg_bp_hi_hz = PPG_BP_HI_HZ,
      .ppg_timing_lpf_hz = PPG_TIMING_LPF_HZ,
      .ppg_delay_ref_hz = PPG_DELAY_REF_HZ,
      .ppg_finger_dc_min = PPG_FINGER_DC_MIN,
      .pat_min_us = PAT_MIN_US,
      .pat_max_us = PAT_MAX_US,
      .spo2_a = SPO2_COEF_A,
      .spo2_b = SPO2_COEF_B,
      .spo2_c = SPO2_COEF_C,
      .warmup_us = WARMUP_US,
      .warmup_beats = WARMUP_BEATS,
  };
  return c;
}

// PPG systolic peaks trail each R by `delay_s`.
static void make_truth(truth_t *t, double rr, double duration_s, double delay_s, uint32_t seed)
{
  synth_rng_t rng = {seed};
  t->n = synth_beat_times(t->r, MAX_BEATS, 0.5, 1.0 + duration_s, rr, rr, 0.04, &rng);
  for (int k = 0; k < t->n; k++) {
    t->ppg[k] = t->r[k] + delay_s;
  }
  for (int k = 0; k < t->n; k++) {
    synth_ppg_fiducials(t->ppg, t->n, k, &t->foot[k], &t->peak[k]);
  }
}

typedef struct {
  double ecg_contact_off_s;   // < 0: never
  double finger_off_s;        // < 0: never
  double red_ratio;           // ratio of ratios
  double ppg_noise;
} run_opts_t;

typedef struct {
  int beat_events;
  int pat_events;
  double pat_foot_max_err_ms;
  double pat_peak_max_err_ms;
  vitals_t v_mid;             // snapshot just before any contact change
  vitals_t v_end;
  bool hr_valid_before_warmup;
} run_result_t;

static int nearest(const double *arr, int n, double t)
{
  int best = 0;
  for (int k = 1; k < n; k++) {
    if (fabs(arr[k] - t) < fabs(arr[best] - t)) {
      best = k;
    }
  }
  return best;
}

static run_result_t run_pipeline(const truth_t *tr, double duration_s, run_opts_t o)
{
  static dsp_pipeline_t d;
  const dsp_config_t cfg = app_dsp_config();
  dsp_pipeline_init(&d, &cfg);
  run_result_t res = {0};

  const synth_ecg_noise_t nz = {0.3, 0.2, 0.02, 5243.0, 1.0};
  synth_rng_t ecg_rng = {1u}, red_rng = {2u}, ir_rng = {3u};
  const double r_offset = synth_ecg_r_offset();
  const double change_s = fmin(o.ecg_contact_off_s < 0 ? 1e9 : o.ecg_contact_off_s,
                               o.finger_off_s < 0 ? 1e9 : o.finger_off_s);

  int ie = 0, ip = 0;
  const double t0 = 1.0;
  bool mid_taken = false;
  for (;;) {
    const double te = t0 + ie / (double)ECG_SAMPLE_RATE_HZ;
    const double tp = t0 + ip / (double)PPG_SAMPLE_RATE_HZ;
    const double t = fmin(te, tp);
    if (t > t0 + duration_s) {
      break;
    }
    if (!mid_taken && t >= change_s) {
      dsp_pipeline_get_vitals(&d, &res.v_mid);
      mid_taken = true;
    }
    if (te <= tp) {
      const bool contact = o.ecg_contact_off_s < 0 || te < o.ecg_contact_off_s;
      dsp_pipeline_set_ecg_contact(&d, contact, (uint32_t)llround(te * 1e6));
      sample_t s = {(uint32_t)llround(te * 1e6), synth_ecg_counts(te, tr->r, tr->n, &nz, &ecg_rng)};
      dsp_pipeline_push_ecg(&d, &s);
      ie++;
    } else {
      const bool finger = o.finger_off_s < 0 || tp < o.finger_off_s;
      const double ir_dc = finger ? 100000.0 : 2000.0;
      const double red_dc = finger ? 80000.0 : 1500.0;
      const double perf = finger ? 0.01 : 0.0;
      const uint32_t t_us = (uint32_t)llround(tp * 1e6);
      sample_t red = {t_us, synth_ppg_counts(tp, tr->ppg, tr->n, red_dc, perf * o.red_ratio,
                                             o.ppg_noise, &red_rng)};
      sample_t ir = {t_us, synth_ppg_counts(tp, tr->ppg, tr->n, ir_dc, perf, o.ppg_noise, &ir_rng)};
      dsp_pipeline_push_ppg(&d, &red, &ir);
      ip++;
      if (tp < t0 + 2.0) {
        vitals_t v;
        dsp_pipeline_get_vitals(&d, &v);
        res.hr_valid_before_warmup |= (v.flags & VITALS_FLAG_HR_VALID) != 0;
      }
    }

    beat_event_t e;
    while (dsp_pipeline_pop_beat(&d, &e)) {
      res.beat_events++;
      if (!(e.flags & VITALS_FLAG_PAT_VALID)) {
        continue;
      }
      res.pat_events++;
      const int k = nearest(tr->r, tr->n, e.r_peak_t_us / 1e6 - r_offset);
      const double r_true = tr->r[k] + r_offset;
      const double fe = fabs(e.pat_foot_us / 1e6 - (tr->foot[k] - r_true)) * 1e3;
      const double pe = fabs(e.pat_peak_us / 1e6 - (tr->peak[k] - r_true)) * 1e3;
      if (fe > res.pat_foot_max_err_ms) res.pat_foot_max_err_ms = fe;
      if (pe > res.pat_peak_max_err_ms) res.pat_peak_max_err_ms = pe;
    }
  }
  dsp_pipeline_get_vitals(&d, &res.v_end);
  if (!mid_taken) {
    res.v_mid = res.v_end;
  }
  return res;
}

static void test_pipeline_pat_hr_pr_spo2(void)
{
  static truth_t tr;
  const double rr = 60.0 / 70.0;
  make_truth(&tr, rr, 60.0, 0.250, 1234u);
  run_opts_t o = {-1.0, -1.0, 0.6, 2.0};
  run_result_t r = run_pipeline(&tr, 60.0, o);

  const double r_offset = synth_ecg_r_offset();
  const int k = tr.n / 2;
  const double pat_foot_true_ms = (tr.foot[k] - tr.r[k] - r_offset) * 1e3;
  const double pat_peak_true_ms = (tr.peak[k] - tr.r[k] - r_offset) * 1e3;

  char msg[240];
  snprintf(msg, sizeof(msg),
           "beats=%d pat_events=%d | per-beat PAT err max foot %.2f peak %.2f ms | vitals HR %.1f "
           "PR %.1f SpO2 %.2f R %.3f PAT foot %u (true %.1f) peak %u (true %.1f) flags 0x%02X",
           r.beat_events, r.pat_events, r.pat_foot_max_err_ms, r.pat_peak_max_err_ms,
           r.v_end.hr_bpm, r.v_end.pr_bpm, r.v_end.spo2_pct, r.v_end.r_ratio,
           (unsigned)r.v_end.pat_foot_us, pat_foot_true_ms * 1e3, (unsigned)r.v_end.pat_peak_us,
           pat_peak_true_ms * 1e3, r.v_end.flags);
  TEST_MESSAGE(msg);

  TEST_ASSERT_FALSE(r.hr_valid_before_warmup);
  const uint8_t all = VITALS_FLAG_ECG_CONTACT | VITALS_FLAG_PPG_FINGER | VITALS_FLAG_HR_VALID |
                      VITALS_FLAG_PR_VALID | VITALS_FLAG_SPO2_VALID | VITALS_FLAG_PAT_VALID;
  TEST_ASSERT_EQUAL_HEX8(all, r.v_end.flags);

  TEST_ASSERT_TRUE(r.beat_events >= 60);
  TEST_ASSERT_TRUE(r.pat_events >= r.beat_events - 6);
  TEST_ASSERT_TRUE(r.pat_foot_max_err_ms <= 4.0);
  TEST_ASSERT_TRUE(r.pat_peak_max_err_ms <= 4.0);

  TEST_ASSERT_FLOAT_WITHIN(4.0, pat_foot_true_ms, r.v_end.pat_foot_us / 1e3);
  TEST_ASSERT_FLOAT_WITHIN(4.0, pat_peak_true_ms, r.v_end.pat_peak_us / 1e3);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 70.0f, r.v_end.hr_bpm);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 70.0f, r.v_end.pr_bpm);
  TEST_ASSERT_FLOAT_WITHIN(0.02f * 0.6f, 0.6f, r.v_end.r_ratio);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, spo2_from_ratio(SPO2_COEF_A, SPO2_COEF_B, SPO2_COEF_C, 0.6f),
                           r.v_end.spo2_pct);
}

static void test_contact_loss_clears_ecg_vitals(void)
{
  static truth_t tr;
  make_truth(&tr, 0.8, 40.0, 0.250, 555u);
  run_opts_t o = {30.0, -1.0, 0.6, 2.0};
  run_result_t r = run_pipeline(&tr, 40.0, o);

  TEST_ASSERT_TRUE(r.v_mid.flags & VITALS_FLAG_HR_VALID);
  TEST_ASSERT_TRUE(r.v_mid.flags & VITALS_FLAG_PAT_VALID);
  TEST_ASSERT_FALSE(r.v_end.flags & VITALS_FLAG_ECG_CONTACT);
  TEST_ASSERT_FALSE(r.v_end.flags & VITALS_FLAG_HR_VALID);
  TEST_ASSERT_FALSE(r.v_end.flags & VITALS_FLAG_PAT_VALID);
  TEST_ASSERT_TRUE(r.v_end.flags & VITALS_FLAG_PR_VALID);
  TEST_ASSERT_TRUE(r.v_end.flags & VITALS_FLAG_SPO2_VALID);
}

static void test_finger_removal_clears_ppg_vitals(void)
{
  static truth_t tr;
  make_truth(&tr, 0.8, 40.0, 0.250, 666u);
  run_opts_t o = {-1.0, 30.0, 0.6, 2.0};
  run_result_t r = run_pipeline(&tr, 40.0, o);

  TEST_ASSERT_TRUE(r.v_mid.flags & VITALS_FLAG_SPO2_VALID);
  TEST_ASSERT_FALSE(r.v_end.flags & VITALS_FLAG_PPG_FINGER);
  TEST_ASSERT_FALSE(r.v_end.flags & VITALS_FLAG_PR_VALID);
  TEST_ASSERT_FALSE(r.v_end.flags & VITALS_FLAG_SPO2_VALID);
  TEST_ASSERT_FALSE(r.v_end.flags & VITALS_FLAG_PAT_VALID);
  TEST_ASSERT_TRUE(r.v_end.flags & VITALS_FLAG_HR_VALID);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_median_and_mad);
  RUN_TEST(test_rate_tracker);
  RUN_TEST(test_pat_matches_latest_r_in_window);
  RUN_TEST(test_pat_flags_outliers);
  RUN_TEST(test_pipeline_pat_hr_pr_spo2);
  RUN_TEST(test_contact_loss_clears_ecg_vitals);
  RUN_TEST(test_finger_removal_clears_ppg_vitals);
  return UNITY_END();
}
