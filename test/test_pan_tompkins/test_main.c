#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <unity.h>

#include "../common/synth.h"
#include "ecg_filter.h"
#include "pan_tompkins.h"

void setUp(void) {}
void tearDown(void) {}

#define FS 256.0
#define MAX_BEATS 512
#define MATCH_WINDOW_S 0.075
#define SCORE_START_S 3.5

typedef struct {
  int tp, fn, fp;
  double max_err_samples;
  double mean_abs_err_samples;
  int searchbacks;
} score_t;

static ecg_filter_t make_filter(void)
{
  ecg_filter_config_t cfg = {
      .fs = (float)FS,
      .bandpass_enable = true,
      .hpf_hz = 0.5f,
      .lpf_hz = 40.0f,
      .notch_enable = true,
      .notch_hz = 60.0f,
      .notch_q = 30.0f,
      .delay_ref_hz = 10.0f,
  };
  ecg_filter_t f;
  ecg_filter_init(&f, &cfg);
  return f;
}

static int run_detector(const double *r, int n_beats, double duration_s,
                        const synth_ecg_noise_t *nz, uint32_t seed, double *det, int max_det,
                        int *searchbacks)
{
  ecg_filter_t f = make_filter();
  pan_tompkins_t pt;
  pan_tompkins_config_t cfg = {
      .fs = (float)FS,
      .input_delay_samples = ecg_filter_delay_samples(&f),
      .learning_s = 2.0f,
  };
  pan_tompkins_init(&pt, &cfg);

  synth_rng_t rng = {seed};
  int n_det = 0;
  *searchbacks = 0;
  const int total = (int)(duration_s * FS);
  for (int i = 0; i < total; i++) {
    const double t = 1.0 + i / FS;
    const uint32_t t_us = (uint32_t)llround(t * 1e6);
    const float x = ecg_filter_process(&f, (float)synth_ecg_counts(t, r, n_beats, nz, &rng));
    pt_beat_t beat;
    if (pan_tompkins_process(&pt, x, t_us, &beat) && n_det < max_det) {
      det[n_det++] = beat.t_us / 1e6;
      *searchbacks += beat.searchback ? 1 : 0;
    }
  }
  return n_det;
}

static score_t score(const double *r, int n_beats, const double *det, int n_det,
                     double end_s)
{
  score_t s = {0};
  const double offset = synth_ecg_r_offset();
  int matched[MAX_BEATS] = {0};
  double sum_err = 0.0;

  for (int i = 0; i < n_beats; i++) {
    const double truth = r[i] + offset;
    if (truth < SCORE_START_S || truth > end_s - 0.5) {
      continue;
    }
    int best = -1;
    double best_err = MATCH_WINDOW_S;
    for (int j = 0; j < n_det; j++) {
      const double err = fabs(det[j] - truth);
      if (!matched[j] && err <= best_err) {
        best = j;
        best_err = err;
      }
    }
    if (best < 0) {
      s.fn++;
      continue;
    }
    matched[best] = 1;
    s.tp++;
    const double err_samples = best_err * FS;
    sum_err += err_samples;
    if (err_samples > s.max_err_samples) {
      s.max_err_samples = err_samples;
    }
  }
  for (int j = 0; j < n_det; j++) {
    if (!matched[j] && det[j] >= SCORE_START_S && det[j] <= end_s - 0.5) {
      s.fp++;
    }
  }
  s.mean_abs_err_samples = s.tp ? sum_err / s.tp : 0.0;
  return s;
}

static score_t evaluate(double rr_start, double rr_end, double jitter, double duration_s,
                        synth_ecg_noise_t nz, uint32_t seed, const char *label)
{
  static double r[MAX_BEATS];
  static double det[MAX_BEATS];
  synth_rng_t rng = {seed};
  const int n_beats =
      synth_beat_times(r, MAX_BEATS, 0.5, 1.0 + duration_s, rr_start, rr_end, jitter, &rng);
  int searchbacks = 0;
  const int n_det = run_detector(r, n_beats, duration_s, &nz, seed * 7u + 1u, det, MAX_BEATS,
                                 &searchbacks);
  score_t s = score(r, n_beats, det, n_det, 1.0 + duration_s);
  s.searchbacks = searchbacks;

  char msg[200];
  snprintf(msg, sizeof(msg),
           "%s: TP=%d FN=%d FP=%d Se=%.2f%% PPV=%.2f%% err mean=%.2f max=%.2f samples sb=%d",
           label, s.tp, s.fn, s.fp, 100.0 * s.tp / (s.tp + s.fn), 100.0 * s.tp / (s.tp + s.fp),
           s.mean_abs_err_samples, s.max_err_samples, s.searchbacks);
  TEST_MESSAGE(msg);
  return s;
}

static void assert_performance(score_t s)
{
  TEST_ASSERT_TRUE(s.tp > 0);
  TEST_ASSERT_TRUE((double)s.tp / (s.tp + s.fn) >= 0.99);
  TEST_ASSERT_TRUE((double)s.tp / (s.tp + s.fp) >= 0.99);
  TEST_ASSERT_TRUE(s.max_err_samples <= 1.0);
}

static const synth_ecg_noise_t kTypical = {
    .baseline_mv = 0.3, .mains_mv = 0.2, .noise_mv = 0.02, .counts_per_mv = 5243.0, .polarity = 1.0};

static void test_steady_72_bpm(void)
{
  assert_performance(evaluate(60.0 / 72.0, 60.0 / 72.0, 0.03, 120.0, kTypical, 11u, "72 bpm"));
}

static void test_inverted_polarity(void)
{
  synth_ecg_noise_t nz = kTypical;
  nz.polarity = -1.0;
  assert_performance(evaluate(60.0 / 72.0, 60.0 / 72.0, 0.03, 120.0, nz, 22u, "inverted"));
}

static void test_rate_ramp_50_to_150_bpm(void)
{
  assert_performance(evaluate(1.2, 0.4, 0.05, 120.0, kTypical, 33u, "50->150 bpm"));
}

static void test_heavy_noise(void)
{
  synth_ecg_noise_t nz = kTypical;
  nz.baseline_mv = 0.8;
  nz.mains_mv = 1.0;
  nz.noise_mv = 0.04;
  assert_performance(evaluate(60.0 / 80.0, 60.0 / 80.0, 0.05, 120.0, nz, 44u, "heavy noise"));
}

static void test_low_amplitude(void)
{
  synth_ecg_noise_t nz = kTypical;
  nz.counts_per_mv = 500.0;  // ~10x weaker dry-electrode signal
  assert_performance(evaluate(60.0 / 65.0, 60.0 / 65.0, 0.03, 120.0, nz, 55u, "low amplitude"));
}

static void test_reset_relearns(void)
{
  static double r[MAX_BEATS];
  synth_rng_t rng = {66u};
  const int n_beats = synth_beat_times(r, MAX_BEATS, 0.5, 20.0, 0.8, 0.8, 0.0, &rng);

  ecg_filter_t f = make_filter();
  pan_tompkins_t pt;
  pan_tompkins_config_t cfg = {(float)FS, ecg_filter_delay_samples(&f), 2.0f};
  pan_tompkins_init(&pt, &cfg);

  synth_rng_t nrng = {67u};
  int before = 0, during_learning = 0, after = 0;
  for (int i = 0; i < (int)(20.0 * FS); i++) {
    const double t = 1.0 + i / FS;
    if (i == (int)(10.0 * FS)) {
      pan_tompkins_reset(&pt);
    }
    const float x = ecg_filter_process(&f, (float)synth_ecg_counts(t, r, n_beats, &kTypical, &nrng));
    pt_beat_t beat;
    if (pan_tompkins_process(&pt, x, (uint32_t)llround(t * 1e6), &beat)) {
      const double since_reset = i / FS - 10.0;
      if (since_reset < 0) before++;
      else if (since_reset < 2.0) during_learning++;
      else after++;
    }
  }
  TEST_ASSERT_TRUE(before >= 9);
  TEST_ASSERT_EQUAL_INT(0, during_learning);
  TEST_ASSERT_TRUE(after >= 9);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_steady_72_bpm);
  RUN_TEST(test_inverted_polarity);
  RUN_TEST(test_rate_ramp_50_to_150_bpm);
  RUN_TEST(test_heavy_noise);
  RUN_TEST(test_low_amplitude);
  RUN_TEST(test_reset_relearns);
  return UNITY_END();
}
