#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <unity.h>

#include "timestamp_recon.h"

void setUp(void) {}
void tearDown(void) {}

// Simulates a sensor whose oscillator runs `drift` slower than nominal and an
// acquisition task that reads 0-3 ms after each captured edge.
static double run_drift_sim(float fs, uint32_t threshold, double drift, double seconds)
{
  ts_recon_t r;
  ts_recon_init(&r, fs, threshold, 0, 0.1f, 0.05f);

  const double ts_true = 1e6 / fs * (1.0 + drift);
  const uint32_t t0 = 4000000000u;  // wraps past 2^32 during the run
  uint32_t next_index = 0;
  uint32_t out[64];
  double worst_error_samples = 0.0;
  srand(1234);

  const uint32_t total = (uint32_t)(seconds * fs);
  while (next_index + threshold < total) {
    const uint32_t edge_index = next_index + threshold - 1;
    const uint32_t t_edge = t0 + (uint32_t)llround(edge_index * ts_true);
    const double read_delay_us = (rand() % 3000);
    uint32_t n = threshold;
    while ((next_index + n) * ts_true <= edge_index * ts_true + read_delay_us && n < 32) {
      n++;
    }

    ts_recon_batch_edge(&r, t_edge, n, out);
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t truth = t0 + (uint32_t)llround((next_index + i) * ts_true);
      const double err = fabs((double)(int32_t)(out[i] - truth)) / ts_true;
      if (err > worst_error_samples) {
        worst_error_samples = err;
      }
    }
    next_index += n;
  }
  return worst_error_samples;
}

static void test_ecg_1pct_drift_within_one_sample(void)
{
  double worst = run_drift_sim(256.0f, 16, 0.01, 60.0);
  TEST_ASSERT_LESS_THAN_DOUBLE(1.0, worst);
}

static void test_ppg_1pct_fast_oscillator_within_one_sample(void)
{
  double worst = run_drift_sim(100.0f, 17, -0.01, 60.0);
  TEST_ASSERT_LESS_THAN_DOUBLE(1.0, worst);
}

static void test_period_estimate_converges(void)
{
  ts_recon_t r;
  ts_recon_init(&r, 256.0f, 16, 0, 0.1f, 0.05f);
  const double ts_true = 1e6 / 256.0 * 1.01;
  uint32_t out[32];
  for (uint32_t batch = 0; batch < 100; batch++) {
    uint32_t edge_index = batch * 16 + 15;
    ts_recon_batch_edge(&r, (uint32_t)llround(edge_index * ts_true), 16, out);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.5f, (float)ts_true, ts_recon_period_us(&r));
}

static void test_outlier_period_rejected(void)
{
  ts_recon_t r;
  ts_recon_init(&r, 100.0f, 17, 0, 0.5f, 0.05f);
  uint32_t out[32];
  ts_recon_batch_edge(&r, 1000000, 17, out);
  ts_recon_batch_edge(&r, 1000000 + 17 * 20000, 17, out);  // 2x period: bogus
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10000.0f, ts_recon_period_us(&r));
}

static void test_polled_continues_from_last_sample(void)
{
  ts_recon_t r;
  ts_recon_init(&r, 100.0f, 17, 0, 0.1f, 0.05f);
  uint32_t out[32];
  ts_recon_batch_edge(&r, 500000, 17, out);
  TEST_ASSERT_EQUAL_UINT32(500000, out[16]);
  ts_recon_batch_polled(&r, 999999999, 3, out);
  TEST_ASSERT_EQUAL_UINT32(510000, out[0]);
  TEST_ASSERT_EQUAL_UINT32(530000, out[2]);
}

static void test_polled_without_history_backfills(void)
{
  ts_recon_t r;
  ts_recon_init(&r, 100.0f, 17, 2000, 0.1f, 0.05f);
  uint32_t out[4];
  ts_recon_batch_polled(&r, 100000, 4, out);
  TEST_ASSERT_EQUAL_UINT32(98000 - 30000, out[0]);
  TEST_ASSERT_EQUAL_UINT32(98000, out[3]);
}

static void test_latency_is_subtracted(void)
{
  ts_recon_t r;
  ts_recon_init(&r, 256.0f, 16, 1500, 0.1f, 0.05f);
  uint32_t out[16];
  ts_recon_batch_edge(&r, 1000000, 16, out);
  TEST_ASSERT_EQUAL_UINT32(1000000 - 1500, out[15]);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_ecg_1pct_drift_within_one_sample);
  RUN_TEST(test_ppg_1pct_fast_oscillator_within_one_sample);
  RUN_TEST(test_period_estimate_converges);
  RUN_TEST(test_outlier_period_rejected);
  RUN_TEST(test_polled_continues_from_last_sample);
  RUN_TEST(test_polled_without_history_backfills);
  RUN_TEST(test_latency_is_subtracted);
  return UNITY_END();
}
