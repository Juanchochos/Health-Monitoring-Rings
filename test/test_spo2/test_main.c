#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <unity.h>

#include "../common/synth.h"
#include "ppg_processor.h"
#include "spo2.h"

void setUp(void) {}
void tearDown(void) {}

// Maxim reference curve (app_config.h).
#define A (-45.060f)
#define B 30.354f
#define C 94.845f

static ppg_beat_t beat(uint32_t t_peak_us, float ac, float dc)
{
  ppg_beat_t b = {t_peak_us - 150000u, t_peak_us, ac, dc};
  return b;
}

static void test_curve_and_clamp(void)
{
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -45.060f * 0.25f + 30.354f * 0.5f + 94.845f,
                           spo2_from_ratio(A, B, C, 0.5f));
  // The Maxim curve peaks just below 100 % near R = 0.34.
  TEST_ASSERT_TRUE(spo2_from_ratio(A, B, C, 0.34f) <= 100.0f);
  TEST_ASSERT_TRUE(spo2_from_ratio(A, B, C, 0.34f) > 99.9f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, spo2_from_ratio(-1.0f, 0.0f, 101.0f, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 70.0f, spo2_from_ratio(A, B, C, 2.0f));
}

static void test_needs_three_paired_beats(void)
{
  spo2_t s;
  spo2_init(&s, A, B, C, 150000u);
  float pct, r;
  TEST_ASSERT_FALSE(spo2_get(&s, &pct, &r));
  for (uint32_t k = 0; k < 3; k++) {
    const uint32_t t = 1000000u + k * 800000u;
    const ppg_beat_t red = beat(t, 600.0f, 80000.0f);       // AC/DC 0.0075
    const ppg_beat_t ir = beat(t + 20000u, 1250.0f, 100000.0f);  // AC/DC 0.0125
    TEST_ASSERT_FALSE(spo2_add_red(&s, &red));
    TEST_ASSERT_TRUE(spo2_add_ir(&s, &ir));
    if (k < 2) {
      TEST_ASSERT_FALSE(spo2_get(&s, &pct, &r));
    }
  }
  TEST_ASSERT_TRUE(spo2_get(&s, &pct, &r));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.6f, r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, spo2_from_ratio(A, B, C, 0.6f), pct);
}

static void test_unpaired_beats_are_ignored(void)
{
  spo2_t s;
  spo2_init(&s, A, B, C, 150000u);
  const ppg_beat_t red = beat(1000000u, 600.0f, 80000.0f);
  const ppg_beat_t ir = beat(1400000u, 1250.0f, 100000.0f);
  spo2_add_red(&s, &red);
  TEST_ASSERT_FALSE(spo2_add_ir(&s, &ir));
  TEST_ASSERT_EQUAL_UINT8(0, s.count);
}

static void test_median_rejects_outlier_beats(void)
{
  spo2_t s;
  spo2_init(&s, A, B, C, 150000u);
  for (uint32_t k = 0; k < 8; k++) {
    const uint32_t t = 1000000u + k * 800000u;
    const float red_ac = (k == 3) ? 3000.0f : 600.0f;  // one motion-corrupted beat
    const ppg_beat_t red = beat(t, red_ac, 80000.0f);
    const ppg_beat_t ir = beat(t, 1250.0f, 100000.0f);
    spo2_add_red(&s, &red);
    spo2_add_ir(&s, &ir);
  }
  float pct, r;
  TEST_ASSERT_TRUE(spo2_get(&s, &pct, &r));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.6f, r);
}

// Red and IR synthetic PPG with a known ratio of ratios through two full
// ppg_processor instances.
static void test_end_to_end_ratio_of_ratios(void)
{
  static double peaks[256];
  synth_rng_t rng = {77u};
  const int n = synth_beat_times(peaks, 256, 0.7, 62.0, 0.8, 0.8, 0.03, &rng);

  ppg_processor_config_t cfg = {100.0f, 0.3f, 0.5f, 5.0f, 8.0f, 1.5f, 2.0f};
  ppg_processor_t red_p, ir_p;
  ppg_processor_init(&red_p, &cfg);
  ppg_processor_init(&ir_p, &cfg);
  spo2_t s;
  spo2_init(&s, A, B, C, 150000u);

  const double r_true[] = {0.5, 0.8, 1.1};
  for (int case_i = 0; case_i < 3; case_i++) {
    ppg_processor_init(&red_p, &cfg);
    ppg_processor_init(&ir_p, &cfg);
    spo2_reset(&s);
    synth_rng_t nr = {88u + (uint32_t)case_i}, ni = {99u + (uint32_t)case_i};
    const double ir_perf = 0.01;
    const double red_perf = r_true[case_i] * ir_perf;
    for (int i = 0; i < 6000; i++) {
      const double t = 1.0 + i / 100.0;
      const uint32_t t_us = (uint32_t)llround(t * 1e6);
      ppg_beat_t rb, ib;
      if (ppg_processor_process(&red_p, (float)synth_ppg_counts(t, peaks, n, 80000.0, red_perf, 3.0, &nr),
                                t_us, &rb)) {
        spo2_add_red(&s, &rb);
      }
      if (ppg_processor_process(&ir_p, (float)synth_ppg_counts(t, peaks, n, 100000.0, ir_perf, 3.0, &ni),
                                t_us, &ib)) {
        spo2_add_ir(&s, &ib);
      }
    }
    float pct, r;
    TEST_ASSERT_TRUE(spo2_get(&s, &pct, &r));
    char msg[120];
    snprintf(msg, sizeof(msg), "R true %.2f measured %.4f (SpO2 %.2f%% vs %.2f%%)", r_true[case_i],
             r, pct, spo2_from_ratio(A, B, C, (float)r_true[case_i]));
    TEST_MESSAGE(msg);
    TEST_ASSERT_FLOAT_WITHIN(0.02f * (float)r_true[case_i], (float)r_true[case_i], r);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, spo2_from_ratio(A, B, C, (float)r_true[case_i]), pct);
  }
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_curve_and_clamp);
  RUN_TEST(test_needs_three_paired_beats);
  RUN_TEST(test_unpaired_beats_are_ignored);
  RUN_TEST(test_median_rejects_outlier_beats);
  RUN_TEST(test_end_to_end_ratio_of_ratios);
  return UNITY_END();
}
