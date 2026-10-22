// Phase 3: GPIOTE interrupts, hardware timestamps and ring buffers.
// The acquisition task fills the ring buffers; this loop drains them into the
// same "timestamp_us,ecg,red,ir" CSV as Phase 2 and prints a '#'-prefixed
// stats line every STATS_PERIOD_MS to verify zero loss.

#include <Arduino.h>

#include "acq_task.h"
#include "app.h"
#include "app_config.h"
#include "hal_led.h"
#include "hal_timestamp.h"
#include "sensor_setup.h"

namespace {

constexpr unsigned kBlinkAcqStartFail = 5;

// ECG is held back until PPG has caught up to its timestamp, unless this much
// ECG has queued (PPG stalled or absent).
constexpr uint32_t kHoldbackSamples = ECG_SAMPLE_RATE_HZ * 2 / 5;

int32_t g_held_red = 0;
int32_t g_held_ir = 0;
uint32_t g_last_stats_ms = 0;
acq_stats_t g_prev_stats = {};

void print_row(uint32_t t_us, int32_t ecg)
{
#if CSV_PLOTTER_MODE
  Serial.printf("%ld,%ld,%ld\n", (long)ecg, (long)g_held_red, (long)g_held_ir);
#else
  Serial.printf("%lu,%ld,%ld,%ld\n", (unsigned long)t_us, (long)ecg, (long)g_held_red,
                (long)g_held_ir);
#endif
}

void drain_to_csv()
{
  ring_buffer_t *rb_ecg = acq_rb_ecg();
  ring_buffer_t *rb_red = acq_rb_ppg_red();
  ring_buffer_t *rb_ir = acq_rb_ppg_ir();

  sample_t ecg, red, ir;
  while (rb_peek(rb_ecg, &ecg)) {
    while (rb_peek(rb_red, &red) && (int32_t)(red.timestamp_us - ecg.timestamp_us) <= 0) {
      rb_pop(rb_red, &red);
      rb_pop(rb_ir, &ir);
      g_held_red = red.raw_sample;
      g_held_ir = ir.raw_sample;
    }
    bool ppg_caught_up = rb_peek(rb_red, &red);
    if (!ppg_caught_up && rb_count(rb_ecg) < kHoldbackSamples) {
      break;
    }
    rb_pop(rb_ecg, &ecg);
    print_row(ecg.timestamp_us, ecg.raw_sample);
  }
}

void print_stats()
{
  acq_stats_t s;
  acq_get_stats(&s);
#if !CSV_PLOTTER_MODE
  const float window_s = STATS_PERIOD_MS / 1000.0f;
  const uint32_t ecg_edges = s.ecg_edges - g_prev_stats.ecg_edges;
  const uint32_t ppg_edges = s.ppg_edges - g_prev_stats.ppg_edges;
  const uint32_t ecg_lat_avg =
      ecg_edges ? (s.ecg_latency_sum_us - g_prev_stats.ecg_latency_sum_us) / ecg_edges : 0;
  const uint32_t ppg_lat_avg =
      ppg_edges ? (s.ppg_latency_sum_us - g_prev_stats.ppg_latency_sum_us) / ppg_edges : 0;

  Serial.printf(
      "# ecg %.1f Hz Ts=%.2fus rb_ovf=%lu fifo_ovf=%lu gaps=%lu polled=%lu lat=%lu/%luus | "
      "ppg %.1f Hz Ts=%.2fus rb_ovf=%lu fifo_ovf=%lu gaps=%lu polled=%lu lat=%lu/%luus | "
      "i2c_err=%lu contact=%d\n",
      (s.ecg_samples - g_prev_stats.ecg_samples) / window_s, s.ecg_period_us,
      (unsigned long)s.ecg_rb_overflows, (unsigned long)s.ecg_fifo_overflows,
      (unsigned long)s.ecg_gaps, (unsigned long)s.ecg_polled_reads, (unsigned long)ecg_lat_avg,
      (unsigned long)s.ecg_latency_max_us, (s.ppg_samples - g_prev_stats.ppg_samples) / window_s,
      s.ppg_period_us, (unsigned long)s.ppg_rb_overflows, (unsigned long)s.ppg_fifo_overflows,
      (unsigned long)s.ppg_gaps, (unsigned long)s.ppg_polled_reads, (unsigned long)ppg_lat_avg,
      (unsigned long)s.ppg_latency_max_us, (unsigned long)s.i2c_errors, acq_ecg_contact());
#endif
  g_prev_stats = s;
}

}  // namespace

void phase3_setup()
{
  Serial.begin(SERIAL_BAUD);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }

  hal_led_init();
  sensors_bring_up_or_halt();
  hal_timestamp_init();
  acq_init();
  if (!acq_start(xTaskGetCurrentTaskHandle())) {
    Serial.println("# FATAL: could not start acquisition (GPIOTE/PPI allocation failed)");
    hal_led_halt_with_code(kBlinkAcqStartFail);
  }
  hal_led_set(true);

#if !CSV_PLOTTER_MODE
  Serial.println("# Phase 3: interrupt-driven acquisition");
  Serial.println("timestamp_us,ecg,red,ir");
#endif
  g_last_stats_ms = millis();
}

void phase3_loop()
{
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
  drain_to_csv();

  if (millis() - g_last_stats_ms >= STATS_PERIOD_MS) {
    g_last_stats_ms = millis();
    print_stats();
  }
}
