#include "biquad.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void set_normalized(biquad_t *f, double b0, double b1, double b2, double a0, double a1,
                           double a2)
{
  f->b0 = (float)(b0 / a0);
  f->b1 = (float)(b1 / a0);
  f->b2 = (float)(b2 / a0);
  f->a1 = (float)(a1 / a0);
  f->a2 = (float)(a2 / a0);
  f->z1 = 0.0f;
  f->z2 = 0.0f;
}

void biquad_design_lowpass(biquad_t *f, float fs, float fc, float q)
{
  const double w0 = 2.0 * M_PI * fc / fs;
  const double cw = cos(w0);
  const double alpha = sin(w0) / (2.0 * q);
  set_normalized(f, (1.0 - cw) / 2.0, 1.0 - cw, (1.0 - cw) / 2.0, 1.0 + alpha, -2.0 * cw,
                 1.0 - alpha);
}

void biquad_design_highpass(biquad_t *f, float fs, float fc, float q)
{
  const double w0 = 2.0 * M_PI * fc / fs;
  const double cw = cos(w0);
  const double alpha = sin(w0) / (2.0 * q);
  set_normalized(f, (1.0 + cw) / 2.0, -(1.0 + cw), (1.0 + cw) / 2.0, 1.0 + alpha, -2.0 * cw,
                 1.0 - alpha);
}

void biquad_design_notch(biquad_t *f, float fs, float f0, float q)
{
  const double w0 = 2.0 * M_PI * f0 / fs;
  const double cw = cos(w0);
  const double alpha = sin(w0) / (2.0 * q);
  set_normalized(f, 1.0, -2.0 * cw, 1.0, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
}

void biquad_cascade_init(biquad_cascade_t *c)
{
  memset(c, 0, sizeof(*c));
}

bool biquad_cascade_add(biquad_cascade_t *c, const biquad_t *f)
{
  if (c->count >= BIQUAD_MAX_STAGES) {
    return false;
  }
  c->stage[c->count++] = *f;
  return true;
}

// Butterworth pole-pair quality factors: Q_k = 1 / (2 cos((2k - 1) pi / (2N))).
static bool add_butter(biquad_cascade_t *c, float fs, float fc, uint8_t order, bool highpass)
{
  if (order != 2 && order != 4) {
    return false;
  }
  const uint8_t pairs = order / 2;
  if (c->count + pairs > BIQUAD_MAX_STAGES) {
    return false;
  }
  for (uint8_t k = 1; k <= pairs; k++) {
    const double q = 1.0 / (2.0 * cos((2.0 * k - 1.0) * M_PI / (2.0 * order)));
    biquad_t f;
    if (highpass) {
      biquad_design_highpass(&f, fs, fc, (float)q);
    } else {
      biquad_design_lowpass(&f, fs, fc, (float)q);
    }
    biquad_cascade_add(c, &f);
  }
  return true;
}

bool biquad_cascade_add_butter_lowpass(biquad_cascade_t *c, float fs, float fc, uint8_t order)
{
  return add_butter(c, fs, fc, order, false);
}

bool biquad_cascade_add_butter_highpass(biquad_cascade_t *c, float fs, float fc, uint8_t order)
{
  return add_butter(c, fs, fc, order, true);
}

float biquad_cascade_process(biquad_cascade_t *c, float x)
{
  for (uint8_t i = 0; i < c->count; i++) {
    x = biquad_process(&c->stage[i], x);
  }
  return x;
}

void biquad_cascade_reset(biquad_cascade_t *c)
{
  for (uint8_t i = 0; i < c->count; i++) {
    c->stage[i].z1 = 0.0f;
    c->stage[i].z2 = 0.0f;
  }
}

void biquad_cascade_prime(biquad_cascade_t *c, float x_dc)
{
  double x = x_dc;
  for (uint8_t i = 0; i < c->count; i++) {
    biquad_t *f = &c->stage[i];
    const double den = 1.0 + f->a1 + f->a2;
    const double dc_gain = fabs(den) > 1e-12 ? (f->b0 + f->b1 + f->b2) / den : 0.0;
    const double y = dc_gain * x;
    f->z1 = (float)(y - f->b0 * x);
    f->z2 = (float)(f->b2 * x - f->a2 * y);
    x = y;
  }
}

// Evaluates P(e^-jw) = c0 + c1 e^-jw + c2 e^-2jw and its group-delay term
// Re{ sum k c_k e^-jwk / P }.
static void poly_eval(double c0, double c1, double c2, double w, double *mag, double *gd)
{
  const double re = c0 + c1 * cos(w) + c2 * cos(2.0 * w);
  const double im = -c1 * sin(w) - c2 * sin(2.0 * w);
  const double dre = c1 * cos(w) + 2.0 * c2 * cos(2.0 * w);
  const double dim = -c1 * sin(w) - 2.0 * c2 * sin(2.0 * w);
  const double den = re * re + im * im;
  *mag = sqrt(den);
  *gd = den > 1e-300 ? (dre * re + dim * im) / den : 0.0;
}

float biquad_cascade_gain(const biquad_cascade_t *c, float fs, float f)
{
  const double w = 2.0 * M_PI * f / fs;
  double gain = 1.0;
  for (uint8_t i = 0; i < c->count; i++) {
    const biquad_t *s = &c->stage[i];
    double mb, ma, gb, ga;
    poly_eval(s->b0, s->b1, s->b2, w, &mb, &gb);
    poly_eval(1.0, s->a1, s->a2, w, &ma, &ga);
    gain *= ma > 0.0 ? mb / ma : 0.0;
  }
  return (float)gain;
}

float biquad_cascade_group_delay(const biquad_cascade_t *c, float fs, float f)
{
  const double w = 2.0 * M_PI * f / fs;
  double delay = 0.0;
  for (uint8_t i = 0; i < c->count; i++) {
    const biquad_t *s = &c->stage[i];
    double mb, ma, gb, ga;
    poly_eval(s->b0, s->b1, s->b2, w, &mb, &gb);
    poly_eval(1.0, s->a1, s->a2, w, &ma, &ga);
    delay += gb - ga;
  }
  return (float)delay;
}
