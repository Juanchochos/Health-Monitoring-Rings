#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unity.h>

#include "../common/synth.h"
#include "ppg_processor.h"

void setUp(void) {}
void tearDown(void) {}

#define FS 100.0
#define MAX_BEATS 512
#define SCORE_START_S 3.5

static ppg_processor_config_t make_cfg(void)
{
  ppg_processor_config_t cfg = {
      .fs = (float)FS,
      .dc_lpf_hz = 0.3f,
      .bp_lo_hz = 0.5f,
      .bp_hi_hz = 5.0f,
      .timing_lpf_hz = 8.0f,
      .delay_ref_hz = 1.5f,
      .learning_s = 2.0f,
  };
  return cfg;
}

typedef struct {
  int truth;
  int matched;
  int extra;
  double foot_max_ms, foot_rms_ms, foot_mean_ms;
  double peak_max_ms, peak_rms_ms, peak_mean_ms;
  double mean_ac_over_dc;
} ppg_score_t;

typedef struct {
  double rr_start, rr_end, jitter;
  double drift_frac;     // 0.1 Hz multiplicative baseline drift
  double noise_counts;   // white noise RMS on ~1000-count pulses
  uint32_t seed;
} scenario_t;

static ppg_score_t evaluate(scenario_t sc, const char *label)
{
  static double peaks[MAX_BEATS];
  static double foot_truth[MAX_BEATS], peak_truth[MAX_BEATS];
  static int used[MAX_BEATS];
  const double duration_s = 120.0;
  synth_rng_t rng = {sc.seed};
  const int n = synth_beat_times(peaks, MAX_BEATS, 0.7, 1.0 + duration_s, sc.rr_start,
                                 sc.rr_end, sc.jitter, &rng);
  for (int k = 0; k < n; k++) {
    synth_ppg_fiducials(peaks, n, k, &foot_truth[k], &peak_truth[k]);
    used[k] = 0;
  }

  ppg_processor_t p;
  ppg_processor_config_t cfg = make_cfg();
  ppg_processor_init(&p, &cfg);

  ppg_score_t s = {0};
  double ac_dc_sum = 0, fs = 0, fss = 0, ps = 0, pss = 0;
  synth_rng_t nrng = {sc.seed + 99u};
  for (int i = 0; i < (int)(duration_s * FS); i++) {
    const double t = 1.0 + i / FS;
    const double dc_t = 100000.0 * (1.0 + sc.drift_frac * sin(2.0 * M_PI * 0.1 * t));
    const int32_t raw = synth_ppg_counts(t, peaks, n, dc_t, 0.01, sc.noise_counts, &nrng);
    ppg_beat_t b;
    if (!ppg_processor_process(&p, (float)raw, (uint32_t)llround(t * 1e6), &b)) {
      continue;
    }
    const double tp = b.t_peak_us / 1e6;
    if (tp < SCORE_START_S) {
      continue;
    }
    int best = -1;
    for (int k = 0; k < n; k++) {
      if (!used[k] && fabs(peak_truth[k] - tp) < 0.15) {
        best = k;
        break;
      }
    }
    if (best < 0) {
      s.extra++;
      continue;
    }
    used[best] = 1;
    s.matched++;
    const double fe = (b.t_foot_us / 1e6 - foot_truth[best]) * 1e3;
    const double pe = (tp - peak_truth[best]) * 1e3;
    fs += fe;
    fss += fe * fe;
    ps += pe;
    pss += pe * pe;
    if (fabs(fe) > s.foot_max_ms) s.foot_max_ms = fabs(fe);
    if (fabs(pe) > s.peak_max_ms) s.peak_max_ms = fabs(pe);
    ac_dc_sum += b.ac_pp / b.dc;
  }
  for (int k = 0; k < n; k++) {
    if (peak_truth[k] >= SCORE_START_S && peak_truth[k] < 1.0 + duration_s - 2.5) {
      s.truth++;
    }
  }
  if (s.matched) {
    s.foot_mean_ms = fs / s.matched;
    s.foot_rms_ms = sqrt(fss / s.matched);
    s.peak_mean_ms = ps / s.matched;
    s.peak_rms_ms = sqrt(pss / s.matched);
    s.mean_ac_over_dc = ac_dc_sum / s.matched;
  }

  char msg[240];
  snprintf(msg, sizeof(msg),
           "%s: truth=%d matched=%d extra=%d | foot mean %+.2f rms %.2f max %.2f ms | "
           "peak mean %+.2f rms %.2f max %.2f ms | AC/DC %.4f",
           label, s.truth, s.matched, s.extra, s.foot_mean_ms, s.foot_rms_ms, s.foot_max_ms,
           s.peak_mean_ms, s.peak_rms_ms, s.peak_max_ms, s.mean_ac_over_dc);
  TEST_MESSAGE(msg);
  return s;
}

static void assert_detection(ppg_score_t s)
{
  TEST_ASSERT_TRUE(s.truth > 50);
  TEST_ASSERT_TRUE(s.matched >= s.truth);
  TEST_ASSERT_EQUAL_INT(0, s.extra);
}

static void test_clean_75_bpm_within_4ms_per_beat(void)
{
  ppg_score_t s = evaluate((scenario_t){0.8, 0.8, 0.03, 0.0, 1.0, 101u}, "75 bpm low noise");
  assert_detection(s);
  TEST_ASSERT_TRUE(s.foot_max_ms <= 4.0);
  TEST_ASSERT_TRUE(s.peak_max_ms <= 4.0);
  TEST_ASSERT_TRUE(s.mean_ac_over_dc > 0.005 && s.mean_ac_over_dc < 0.02);
}

static void test_rate_ramp_50_to_120_bpm(void)
{
  ppg_score_t s = evaluate((scenario_t){1.2, 0.5, 0.04, 0.0, 1.0, 202u}, "50->120 bpm");
  assert_detection(s);
  TEST_ASSERT_TRUE(s.foot_max_ms <= 4.0);
  // Near 120 bpm the dicrotic wave overlaps the next upstroke and skews the
  // detrend anchors slightly.
  TEST_ASSERT_TRUE(s.peak_rms_ms <= 1.0);
  TEST_ASSERT_TRUE(s.peak_max_ms <= 5.0);
}

static void test_baseline_drift_is_detrended(void)
{
  ppg_score_t s = evaluate((scenario_t){0.85, 0.85, 0.03, 0.02, 1.0, 303u}, "2% DC drift");
  assert_detection(s);
  TEST_ASSERT_TRUE(s.foot_max_ms <= 4.0);
  TEST_ASSERT_TRUE(s.peak_max_ms <= 4.0);
}

static void test_moderate_noise(void)
{
  ppg_score_t s = evaluate((scenario_t){0.8, 0.8, 0.03, 0.02, 5.0, 404u}, "noise 5 counts");
  assert_detection(s);
  TEST_ASSERT_TRUE(fabs(s.foot_mean_ms) <= 2.0);
  TEST_ASSERT_TRUE(s.foot_rms_ms <= 4.0);
  TEST_ASSERT_TRUE(s.peak_rms_ms <= 2.0);
}

static void test_heavy_noise_still_detects(void)
{
  ppg_score_t s = evaluate((scenario_t){0.75, 0.75, 0.05, 0.0, 30.0, 505u}, "noise 30 counts");
  TEST_ASSERT_TRUE(s.matched >= s.truth);
  TEST_ASSERT_TRUE(s.extra <= s.truth / 100 + 1);
  TEST_ASSERT_TRUE(s.foot_rms_ms <= 15.0);
  TEST_ASSERT_TRUE(s.peak_rms_ms <= 6.0);
}

static void test_dc_tracks_step(void)
{
  ppg_processor_t p;
  ppg_processor_config_t cfg = make_cfg();
  ppg_processor_init(&p, &cfg);
  ppg_beat_t b;
  for (int i = 0; i < (int)(10.0 * FS); i++) {
    ppg_processor_process(&p, 2000.0f, (uint32_t)(i * 10000), &b);
  }
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 2000.0f, ppg_processor_dc(&p));
  for (int i = 0; i < (int)(5.0 * FS); i++) {
    ppg_processor_process(&p, 120000.0f, (uint32_t)(i * 10000), &b);
  }
  TEST_ASSERT_FLOAT_WITHIN(500.0f, 120000.0f, ppg_processor_dc(&p));
}

static void test_constant_signal_has_no_beats(void)
{
  ppg_processor_t p;
  ppg_processor_config_t cfg = make_cfg();
  ppg_processor_init(&p, &cfg);
  int beats = 0;
  for (int i = 0; i < (int)(30.0 * FS); i++) {
    ppg_beat_t b;
    beats += ppg_processor_process(&p, 100000.0f, (uint32_t)(i * 10000), &b) ? 1 : 0;
  }
  TEST_ASSERT_EQUAL_INT(0, beats);
}

static void test_last_beat_released_by_timeout(void)
{
  static double peaks[64];
  synth_rng_t rng = {606u};
  const int n = synth_beat_times(peaks, 64, 0.7, 10.0, 0.8, 0.8, 0.0, &rng);
  ppg_processor_t p;
  ppg_processor_config_t cfg = make_cfg();
  ppg_processor_init(&p, &cfg);
  int beats = 0;
  bool last_seen = false;
  for (int i = 0; i < (int)(14.0 * FS); i++) {
    const double t = 1.0 + i / FS;
    ppg_beat_t b;
    if (ppg_processor_process(&p, (float)synth_ppg_counts(t, peaks, n, 100000.0, 0.01, 0.0, &rng),
                              (uint32_t)llround(t * 1e6), &b)) {
      beats++;
      last_seen |= fabs(b.t_peak_us / 1e6 - peaks[n - 1]) < 0.01;
    }
  }
  TEST_ASSERT_TRUE(beats >= 8);
  TEST_ASSERT_TRUE(last_seen);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_clean_75_bpm_within_4ms_per_beat);
  RUN_TEST(test_rate_ramp_50_to_120_bpm);
  RUN_TEST(test_baseline_drift_is_detrended);
  RUN_TEST(test_moderate_noise);
  RUN_TEST(test_heavy_noise_still_detects);
  RUN_TEST(test_dc_tracks_step);
  RUN_TEST(test_constant_signal_has_no_beats);
  RUN_TEST(test_last_beat_released_by_timeout);
  return UNITY_END();
}
