#pragma once

#include <stdint.h>

typedef struct {
  uint32_t timestamp_us;
  int32_t raw_sample;
} sample_t;
