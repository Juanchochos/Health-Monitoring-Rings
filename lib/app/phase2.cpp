// Phase 2: register configuration and polling baseline.
// Polls both FIFOs every 10 ms and streams one CSV line per ECG sample:
//   timestamp_us,ecg,red,ir
// using the most recent PPG sample at or before each ECG sample. Timestamps
// are micros() at read time, back-filled by the nominal sample period; Phase 3
// replaces them with hardware-captured edges. Lines starting with '#' are
// diagnostics and are suppressed in CSV_PLOTTER_MODE.

#include <Arduino.h>

#include "app.h"
#include "app_config.h"
#include "hal_led.h"
#include "max30003/max30003.h"
#include "max30102/max30102.h"
#include "sensor_setup.h"

namespace {

constexpr uint32_t kPollPeriodMs = 10;
constexpr uint32_t kEcgPeriodUs = 1000000UL / ECG_SAMPLE_RATE_HZ;
constexpr uint32_t kPpgPeriodUs = 1000000UL / PPG_SAMPLE_RATE_HZ;

uint32_t g_held_red = 0;
uint32_t g_held_ir = 0;

uint32_t g_last_poll_ms = 0;
uint32_t g_last_stats_ms = 0;
uint32_t g_ecg_count = 0;
uint32_t g_ppg_count = 0;
uint32_t g_ecg_overflows = 0;
uint32_t g_ppg_overflows = 0;

void print_row(uint32_t t_us, int32_t ecg, uint32_t red, uint32_t ir)
{
#if CSV_PLOTTER_MODE
  Serial.printf("%ld,%lu,%lu\n", (long)ecg, (unsigned long)red, (unsigned long)ir);
#else
  Serial.printf("%lu,%ld,%lu,%lu\n", (unsigned long)t_us, (long)ecg, (unsigned long)red,
                (unsigned long)ir);
#endif
}

void poll_once()
{
  const uint32_t now_us = micros();

  uint32_t red[MAX30102_FIFO_DEPTH], ir[MAX30102_FIFO_DEPTH];
  max30102_fifo_result_t ppg = max30102_read_fifo(red, ir, MAX30102_FIFO_DEPTH);
  g_ppg_overflows += ppg.overflow;
  g_ppg_count += ppg.count;

  uint32_t status = max30003_read_status();
  int32_t ecg[MAX30003_FIFO_DEPTH];
  max30003_fifo_result_t ecg_res = max30003_read_fifo(ecg, MAX30003_FIFO_DEPTH);
  if (ecg_res.overflow || (status & MAX30003_STATUS_EOVF)) {
    max30003_fifo_reset();
    g_ecg_overflows++;
  }
  g_ecg_count += ecg_res.count;

  uint8_t next_ppg = 0;
  for (uint8_t i = 0; i < ecg_res.count; i++) {
    uint32_t t_ecg = now_us - (uint32_t)(ecg_res.count - 1 - i) * kEcgPeriodUs;
    while (next_ppg < ppg.count) {
      uint32_t t_ppg = now_us - (uint32_t)(ppg.count - 1 - next_ppg) * kPpgPeriodUs;
      if ((int32_t)(t_ppg - t_ecg) > 0) {
        break;
      }
      g_held_red = red[next_ppg];
      g_held_ir = ir[next_ppg];
      next_ppg++;
    }
    print_row(t_ecg, ecg[i], g_held_red, g_held_ir);
  }
  if (ppg.count > 0) {
    g_held_red = red[ppg.count - 1];
    g_held_ir = ir[ppg.count - 1];
  }
}

void print_stats()
{
#if !CSV_PLOTTER_MODE
  const float window_s = STATS_PERIOD_MS / 1000.0f;
  Serial.printf("# rates ecg=%.1f Hz ppg=%.1f Hz | overflows ecg=%lu ppg=%lu\n",
                g_ecg_count / window_s, g_ppg_count / window_s,
                (unsigned long)g_ecg_overflows, (unsigned long)g_ppg_overflows);
#endif
  g_ecg_count = 0;
  g_ppg_count = 0;
}

}  // namespace

void phase2_setup()
{
  Serial.begin(SERIAL_BAUD);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }

  hal_led_init();
  sensors_bring_up_or_halt();
  hal_led_set(true);

#if !CSV_PLOTTER_MODE
  Serial.println("# Phase 2: polling baseline");
  Serial.println("timestamp_us,ecg,red,ir");
#endif
  g_last_poll_ms = millis();
  g_last_stats_ms = g_last_poll_ms;
}

void phase2_loop()
{
  uint32_t now = millis();
  if (now - g_last_poll_ms >= kPollPeriodMs) {
    g_last_poll_ms = now;
    poll_once();
  }
  if (now - g_last_stats_ms >= STATS_PERIOD_MS) {
    g_last_stats_ms = now;
    print_stats();
  }
  delay(1);
}
