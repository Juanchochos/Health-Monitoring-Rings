#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Second-order IIR sections (Direct Form II transposed), designed at init from
// fs and corner frequency with the RBJ bilinear formulas, so a sample-rate
// change needs no regenerated coefficient tables.

#define BIQUAD_MAX_STAGES 4

typedef struct {
  float b0, b1, b2, a1, a2;
  float z1, z2;
} biquad_t;

typedef struct {
  biquad_t stage[BIQUAD_MAX_STAGES];
  uint8_t count;
} biquad_cascade_t;

void biquad_design_lowpass(biquad_t *f, float fs, float fc, float q);
void biquad_design_highpass(biquad_t *f, float fs, float fc, float q);
void biquad_design_notch(biquad_t *f, float fs, float f0, float q);

static inline float biquad_process(biquad_t *f, float x)
{
  const float y = f->b0 * x + f->z1;
  f->z1 = f->b1 * x - f->a1 * y + f->z2;
  f->z2 = f->b2 * x - f->a2 * y;
  return y;
}

void biquad_cascade_init(biquad_cascade_t *c);
bool biquad_cascade_add(biquad_cascade_t *c, const biquad_t *f);

// Append an order-2 or order-4 Butterworth section set.
bool biquad_cascade_add_butter_lowpass(biquad_cascade_t *c, float fs, float fc, uint8_t order);
bool biquad_cascade_add_butter_highpass(biquad_cascade_t *c, float fs, float fc, uint8_t order);

float biquad_cascade_process(biquad_cascade_t *c, float x);
void biquad_cascade_reset(biquad_cascade_t *c);

// Load the state that a constant input x_dc would have reached, so a large
// sensor offset does not produce a long start-up transient.
void biquad_cascade_prime(biquad_cascade_t *c, float x_dc);

// Magnitude response (linear) and group delay (samples) at frequency f.
float biquad_cascade_gain(const biquad_cascade_t *c, float fs, float f);
float biquad_cascade_group_delay(const biquad_cascade_t *c, float fs, float f);

#ifdef __cplusplus
}
#endif
