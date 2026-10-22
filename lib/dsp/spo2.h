#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ppg_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

// SpO2 from the ratio of ratios R = (AC_red / DC_red) / (AC_ir / DC_ir).
// Red and IR beats are paired when their systolic peaks fall within
// pair_window_us; the output uses the median R of the last SPO2_HISTORY beats
// mapped through SpO2 = a R^2 + b R + c, clamped to 70-100 %.

#define SPO2_HISTORY 8
#define SPO2_MIN_BEATS 3

typedef struct {
  float a, b, c;
  uint32_t pair_window_us;
  float r_hist[SPO2_HISTORY];
  uint8_t count;
  uint8_t pos;
  ppg_beat_t red;
  ppg_beat_t ir;
  bool have_red;
  bool have_ir;
} spo2_t;

void spo2_init(spo2_t *s, float a, float b, float c, uint32_t pair_window_us);
void spo2_reset(spo2_t *s);

// Each returns true when the beat completed a Red/IR pair and R was updated.
bool spo2_add_red(spo2_t *s, const ppg_beat_t *beat);
bool spo2_add_ir(spo2_t *s, const ppg_beat_t *beat);

// Returns false until SPO2_MIN_BEATS ratios are available.
bool spo2_get(const spo2_t *s, float *spo2_pct, float *ratio);

float spo2_from_ratio(float a, float b, float c, float ratio);

#ifdef __cplusplus
}
#endif
