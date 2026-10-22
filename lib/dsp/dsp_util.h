#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Median of n values (n <= 32); the input is not modified.
float dsp_median(const float *values, uint8_t n);

// Median absolute deviation around `median`.
float dsp_mad(const float *values, uint8_t n, float median);

#ifdef __cplusplus
}
#endif
