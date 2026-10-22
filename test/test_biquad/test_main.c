#include <math.h>
#include <stdint.h>
#include <unity.h>

#include "biquad.h"
#include "ecg_filter.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void setUp(void) {}
void tearDown(void) {}

static const float kFs = 256.0f;

static ecg_filter_config_t ecg_cfg(void)
{
  ecg_filter_config_t cfg = {
      .fs = kFs,
      .bandpass_enable = true,
      .hpf_hz = 0.5f,
      .lpf_hz = 40.0f,
      .notch_enable = true,
      .notch_hz = 60.0f,
      .notch_q = 30.0f,
      .delay_ref_hz = 10.0f,
  };
  return cfg;
}

// Steady-state amplitude ratio of a sinusoid through the cascade, measured
// after the start-up transient has decayed.
static double measured_gain(biquad_cascade_t *c, float fs, float f)
{
  biquad_cascade_reset(c);
  const int settle = (int)(fs * 20.0f);
  const int measure = (int)(fs * 10.0f);
  double peak = 0.0;
  for (int i = 0; i < settle + measure; i++) {
    const float x = (float)sin(2.0 * M_PI * f * i / fs);
    const float y = biquad_cascade_process(c, x);
    if (i >= settle && fabs(y) > peak) {
      peak = fabs(y);
    }
  }
  return peak;
}

static void test_butterworth_lowpass_response(void)
{
  biquad_cascade_t c;
  biquad_cascade_init(&c);
  TEST_ASSERT_TRUE(biquad_cascade_add_butter_lowpass(&c, kFs, 40.0f, 4));
  TEST_ASSERT_EQUAL_UINT8(2, c.count);

  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, biquad_cascade_gain(&c, kFs, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)M_SQRT1_2, biquad_cascade_gain(&c, kFs, 40.0f));
  TEST_ASSERT_TRUE(biquad_cascade_gain(&c, kFs, 100.0f) < 0.05f);
  TEST_ASSERT_FLOAT_WITHIN(0.01, biquad_cascade_gain(&c, kFs, 25.0f),
                           measured_gain(&c, kFs, 25.0f));
}

static void test_butterworth_highpass_response(void)
{
  biquad_cascade_t c;
  biquad_cascade_init(&c);
  TEST_ASSERT_TRUE(biquad_cascade_add_butter_highpass(&c, kFs, 0.5f, 2));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, biquad_cascade_gain(&c, kFs, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)M_SQRT1_2, biquad_cascade_gain(&c, kFs, 0.5f));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, biquad_cascade_gain(&c, kFs, 20.0f));
}

static void test_rejects_unsupported_order(void)
{
  biquad_cascade_t c;
  biquad_cascade_init(&c);
  TEST_ASSERT_FALSE(biquad_cascade_add_butter_lowpass(&c, kFs, 10.0f, 3));
  TEST_ASSERT_TRUE(biquad_cascade_add_butter_lowpass(&c, kFs, 10.0f, 4));
  TEST_ASSERT_TRUE(biquad_cascade_add_butter_lowpass(&c, kFs, 10.0f, 4));
  TEST_ASSERT_FALSE(biquad_cascade_add_butter_lowpass(&c, kFs, 10.0f, 2));
}

static void test_ecg_chain_attenuates_mains_by_30db(void)
{
  ecg_filter_config_t cfg = ecg_cfg();
  ecg_filter_t f;
  TEST_ASSERT_TRUE(ecg_filter_init(&f, &cfg));
  biquad_cascade_t chain = *ecg_filter_chain(&f);

  const double analytic = biquad_cascade_gain(&chain, kFs, 60.0f);
  const double measured = measured_gain(&chain, kFs, 60.0f);
  TEST_ASSERT_TRUE(20.0 * log10(analytic) < -30.0);
  TEST_ASSERT_TRUE(20.0 * log10(measured) < -30.0);

  // QRS band passes essentially untouched.
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, biquad_cascade_gain(&chain, kFs, 10.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, biquad_cascade_gain(&chain, kFs, 20.0f));
}

static void test_prime_removes_startup_transient(void)
{
  biquad_cascade_t lp, hp;
  biquad_cascade_init(&lp);
  biquad_cascade_add_butter_lowpass(&lp, kFs, 5.0f, 4);
  biquad_cascade_init(&hp);
  biquad_cascade_add_butter_highpass(&hp, kFs, 0.5f, 2);

  const float dc = 100000.0f;
  biquad_cascade_prime(&lp, dc);
  biquad_cascade_prime(&hp, dc);
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_FLOAT_WITHIN(dc * 1e-4f, dc, biquad_cascade_process(&lp, dc));
    TEST_ASSERT_FLOAT_WITHIN(dc * 1e-4f, 0.0f, biquad_cascade_process(&hp, dc));
  }
}

// Group delay measured as the energy-centroid shift of a narrowband tone
// burst, compared with the analytic value.
static double measured_group_delay(biquad_cascade_t *c, float fs, float f)
{
  biquad_cascade_reset(c);
  const double centre = 5.0 * fs;
  const double sigma = 1.0 * fs;
  const int n = (int)(10.0 * fs);
  double ex = 0, ey = 0, tx = 0, ty = 0;
  for (int i = 0; i < n; i++) {
    const double env = exp(-0.5 * pow((i - centre) / sigma, 2.0));
    const float x = (float)(env * sin(2.0 * M_PI * f * i / fs));
    const float y = biquad_cascade_process(c, x);
    ex += (double)x * x;
    tx += (double)x * x * i;
    ey += (double)y * y;
    ty += (double)y * y * i;
  }
  return ty / ey - tx / ex;
}

static void test_group_delay_matches_measurement(void)
{
  ecg_filter_config_t cfg = ecg_cfg();
  ecg_filter_t f;
  ecg_filter_init(&f, &cfg);
  biquad_cascade_t chain = *ecg_filter_chain(&f);

  const float analytic = biquad_cascade_group_delay(&chain, kFs, 10.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, analytic, ecg_filter_delay_samples(&f));
  TEST_ASSERT_TRUE(analytic > 0.5f && analytic < 10.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.2, analytic, measured_group_delay(&chain, kFs, 10.0f));

  biquad_cascade_t ppg_lp;
  biquad_cascade_init(&ppg_lp);
  biquad_cascade_add_butter_lowpass(&ppg_lp, 100.0f, 5.0f, 2);
  TEST_ASSERT_FLOAT_WITHIN(0.2, biquad_cascade_group_delay(&ppg_lp, 100.0f, 1.5f),
                           measured_group_delay(&ppg_lp, 100.0f, 1.5f));
}

static void test_bandpass_disabled_leaves_notch_only(void)
{
  ecg_filter_config_t cfg = ecg_cfg();
  cfg.bandpass_enable = false;
  ecg_filter_t f;
  ecg_filter_init(&f, &cfg);
  const biquad_cascade_t *chain = ecg_filter_chain(&f);
  TEST_ASSERT_EQUAL_UINT8(1, chain->count);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, biquad_cascade_gain(chain, kFs, 0.0f));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_butterworth_lowpass_response);
  RUN_TEST(test_butterworth_highpass_response);
  RUN_TEST(test_rejects_unsupported_order);
  RUN_TEST(test_ecg_chain_attenuates_mains_by_30db);
  RUN_TEST(test_prime_removes_startup_transient);
  RUN_TEST(test_group_delay_matches_measurement);
  RUN_TEST(test_bandpass_disabled_leaves_notch_only);
  return UNITY_END();
}
