#pragma once

// Header-only synthetic ECG/PPG generator for host tests. Signals are built
// from Gaussian waves around known beat times, so the true fiducials are
// known exactly (or can be located numerically on the noise-free waveform).

#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
  uint32_t s;
} synth_rng_t;

static inline double synth_uniform(synth_rng_t *r)
{
  r->s ^= r->s << 13;
  r->s ^= r->s >> 17;
  r->s ^= r->s << 5;
  return ((double)r->s + 0.5) / 4294967296.0;
}

static inline double synth_gauss(synth_rng_t *r)
{
  const double u1 = synth_uniform(r);
  const double u2 = synth_uniform(r);
  return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

static inline double synth_g(double t, double c, double s)
{
  const double z = (t - c) / s;
  return exp(-0.5 * z * z);
}

// Beat times in seconds from t_start to t_end. RR ramps linearly from
// rr_start to rr_end, with uniform jitter of +/- jitter_frac.
static inline int synth_beat_times(double *out, int max, double t_start, double t_end,
                                   double rr_start, double rr_end, double jitter_frac,
                                   synth_rng_t *rng)
{
  int n = 0;
  double t = t_start;
  while (n < max && t < t_end) {
    out[n++] = t;
    const double frac = (t - t_start) / (t_end - t_start);
    const double rr = rr_start + (rr_end - rr_start) * frac;
    t += rr * (1.0 + jitter_frac * (2.0 * synth_uniform(rng) - 1.0));
  }
  return n;
}

// ---------------------------------------------------------------- ECG ----

// PQRST morphology in mV, R wave centred at dt = 0.
static inline double synth_ecg_beat(double dt)
{
  if (dt < -0.4 || dt > 0.6) {
    return 0.0;
  }
  return 0.15 * synth_g(dt, -0.20, 0.025) - 0.10 * synth_g(dt, -0.030, 0.008) +
         1.00 * synth_g(dt, 0.0, 0.010) - 0.25 * synth_g(dt, 0.030, 0.008) +
         0.35 * synth_g(dt, 0.28, 0.045);
}

// Noise-free ECG in mV at time t (seconds).
static inline double synth_ecg_clean(double t, const double *r, int n)
{
  double v = 0.0;
  for (int i = 0; i < n; i++) {
    const double dt = t - r[i];
    if (dt < -0.4) {
      break;
    }
    v += synth_ecg_beat(dt);
  }
  return v;
}

// Offset of the morphology's true maximum from the R-wave centre.
static inline double synth_ecg_r_offset(void)
{
  double best_t = 0.0, best_v = -1e9;
  for (double dt = -0.01; dt <= 0.01; dt += 1e-6) {
    const double v = synth_ecg_beat(dt);
    if (v > best_v) {
      best_v = v;
      best_t = dt;
    }
  }
  return best_t;
}

typedef struct {
  double baseline_mv;      // 0.25 Hz wander amplitude
  double mains_mv;         // 60 Hz interference amplitude
  double noise_mv;         // white noise RMS
  double counts_per_mv;    // MAX30003 at 40 V/V: ~5243 counts/mV
  double polarity;         // +1 or -1 (electrode orientation)
} synth_ecg_noise_t;

static inline int32_t synth_ecg_counts(double t, const double *r, int n,
                                       const synth_ecg_noise_t *nz, synth_rng_t *rng)
{
  double mv = nz->polarity * synth_ecg_clean(t, r, n);
  mv += nz->baseline_mv * sin(2.0 * M_PI * 0.25 * t);
  mv += nz->mains_mv * sin(2.0 * M_PI * 60.0 * t + 0.3);
  mv += nz->noise_mv * synth_gauss(rng);
  return (int32_t)lround(mv * nz->counts_per_mv);
}

// ---------------------------------------------------------------- PPG ----

// Absorption pulse (dimensionless, peak ~1) with the systolic peak at dt = 0
// and a dicrotic wave 300 ms later.
static inline double synth_ppg_beat(double dt)
{
  if (dt < -0.6 || dt > 1.0) {
    return 0.0;
  }
  return synth_g(dt, 0.0, 0.09) + 0.4 * synth_g(dt, 0.30, 0.10);
}

static inline double synth_ppg_clean(double t, const double *peaks, int n)
{
  double v = 0.0;
  for (int i = 0; i < n; i++) {
    const double dt = t - peaks[i];
    if (dt < -0.6) {
      break;
    }
    v += synth_ppg_beat(dt);
  }
  return v;
}

static inline double synth_ppg_near(double t, const double *peaks, int n, int k)
{
  const int lo = k - 4 < 0 ? 0 : k - 4;
  const int hi = k + 4 > n ? n : k + 4;
  return synth_ppg_clean(t, peaks + lo, hi - lo);
}

// True fiducials of beat k on the noise-free absorption waveform: systolic
// peak (maximum) and foot (maximum upstroke slope).
static inline void synth_ppg_fiducials(const double *peaks, int n, int k, double *t_foot,
                                       double *t_peak)
{
  const double h = 1e-5;
  double best_v = -1e9;
  for (double t = peaks[k] - 0.05; t <= peaks[k] + 0.05; t += h) {
    const double v = synth_ppg_near(t, peaks, n, k);
    if (v > best_v) {
      best_v = v;
      *t_peak = t;
    }
  }
  double best_s = -1e9;
  for (double t = peaks[k] - 0.3; t <= *t_peak; t += h) {
    const double s = synth_ppg_near(t + h, peaks, n, k) - synth_ppg_near(t - h, peaks, n, k);
    if (s > best_s) {
      best_s = s;
      *t_foot = t;
    }
  }
}

// Raw MAX30102-style counts: light falls as absorption rises.
static inline int32_t synth_ppg_counts(double t, const double *peaks, int n, double dc,
                                       double perfusion, double noise_counts, synth_rng_t *rng)
{
  const double v = dc * (1.0 - perfusion * synth_ppg_clean(t, peaks, n)) +
                   noise_counts * synth_gauss(rng);
  return (int32_t)lround(v);
}
