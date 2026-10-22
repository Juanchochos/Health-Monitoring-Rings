#include "dsp_util.h"

#include <math.h>

#define DSP_MEDIAN_MAX 32

float dsp_median(const float *values, uint8_t n)
{
  if (n == 0) {
    return 0.0f;
  }
  if (n > DSP_MEDIAN_MAX) {
    n = DSP_MEDIAN_MAX;
  }
  float sorted[DSP_MEDIAN_MAX];
  for (uint8_t i = 0; i < n; i++) {
    float v = values[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && sorted[j] > v) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = v;
  }
  return (n & 1) ? sorted[n / 2] : 0.5f * (sorted[n / 2 - 1] + sorted[n / 2]);
}

float dsp_mad(const float *values, uint8_t n, float median)
{
  if (n > DSP_MEDIAN_MAX) {
    n = DSP_MEDIAN_MAX;
  }
  float dev[DSP_MEDIAN_MAX];
  for (uint8_t i = 0; i < n; i++) {
    dev[i] = fabsf(values[i] - median);
  }
  return dsp_median(dev, n);
}
