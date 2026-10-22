// Phase 4: full pipeline.
//   acq task (HIGH)    : INTB/INT edges -> burst FIFO reads -> ring buffers
//   dsp task (NORMAL)  : ring buffers -> filters, QRS, PPG, SpO2, PAT -> queues
//   loop task (LOW)    : queues -> telemetry sinks (USB serial, BLE)

#include <Arduino.h>

#include "acq_task.h"
#include "app.h"
#include "app_config.h"
#include "dsp_task.h"
#include "hal_led.h"
#include "hal_timestamp.h"
#include "sensor_setup.h"
#include "serial_sink.h"
#include "telemetry_sink.h"
#if BLE_ENABLE
#include "ble_sink.h"
#endif

namespace {

constexpr unsigned kBlinkAcqStartFail = 5;
constexpr unsigned kBlinkDspStartFail = 6;
constexpr uint32_t kBeatLedMs = 60;

SerialSink g_serial(TELEMETRY_SERIAL_MODE == TELEM_SERIAL_RAW ? SerialSink::Mode::kRaw
                                                              : SerialSink::Mode::kVitals);
#if BLE_ENABLE
BleSink g_ble(BLE_RAW_STREAM_ENABLE);
#endif
TelemetryFanout g_fanout;

uint8_t g_last_flags = 0;
uint32_t g_last_stats_ms = 0;
uint32_t g_led_off_ms = 0;
bool g_led_on = false;
acq_stats_t g_prev_stats = {};

void publish_status()
{
  acq_stats_t s;
  acq_get_stats(&s);
  const float window_s = (millis() - g_last_stats_ms) / 1000.0f;

  telemetry_status_t st = {};
  st.flags = g_last_flags;
  st.ecg_fifo_overflows = s.ecg_fifo_overflows;
  st.ppg_fifo_overflows = s.ppg_fifo_overflows;
  st.rb_overflows = s.ecg_rb_overflows + s.ppg_rb_overflows;
  st.gaps = s.ecg_gaps + s.ppg_gaps;
  st.i2c_errors = (uint16_t)(s.i2c_errors > 0xFFFF ? 0xFFFF : s.i2c_errors);
  st.ecg_rate_hz = window_s > 0 ? (s.ecg_samples - g_prev_stats.ecg_samples) / window_s : 0;
  st.ppg_rate_hz = window_s > 0 ? (s.ppg_samples - g_prev_stats.ppg_samples) / window_s : 0;
  g_fanout.publish_status(st);
  g_prev_stats = s;
}

void beat_led(const beat_event_t &b)
{
  if (b.flags & VITALS_FLAG_HR_VALID) {
    hal_led_set(true);
    g_led_on = true;
    g_led_off_ms = millis() + kBeatLedMs;
  }
}

}  // namespace

void phase4_setup()
{
  Serial.begin(SERIAL_BAUD);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 2000) {
    delay(10);
  }
  hal_led_init();

  // Sinks start first: BLE brings up the SoftDevice, which must own PPI
  // before hal_timestamp routes the GPIOTE events.
  g_fanout.add(&g_serial);
#if BLE_ENABLE
  g_fanout.add(&g_ble);
#endif
  if (!g_fanout.begin()) {
    Serial.println("# WARN: a telemetry sink failed to start");
  }

  sensors_bring_up_or_halt();
  hal_timestamp_init();
  acq_init();

  if (!dsp_task_start(xTaskGetCurrentTaskHandle())) {
    Serial.println("# FATAL: could not start the DSP task");
    hal_led_halt_with_code(kBlinkDspStartFail);
  }
  if (!acq_start(dsp_task_handle())) {
    Serial.println("# FATAL: could not start acquisition (GPIOTE/PPI allocation failed)");
    hal_led_halt_with_code(kBlinkAcqStartFail);
  }
  Serial.println("# Phase 4: DSP + telemetry running");
  g_last_stats_ms = millis();
}

void phase4_loop()
{
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
  dsp_task_set_raw_enabled(g_fanout.wants_raw());

  raw_record_t raw;
  while (dsp_take_raw(&raw)) {
    g_fanout.publish_raw(raw);
  }
  beat_event_t beat;
  while (dsp_take_beat(&beat)) {
    g_fanout.publish_beat(beat);
    beat_led(beat);
  }
  vitals_t v;
  if (dsp_take_vitals(&v)) {
    g_last_flags = v.flags;
    g_fanout.publish_vitals(v);
  }

  if (g_led_on && (int32_t)(millis() - g_led_off_ms) >= 0) {
    hal_led_set(false);
    g_led_on = false;
  }
  if (millis() - g_last_stats_ms >= STATS_PERIOD_MS) {
    publish_status();
    g_last_stats_ms = millis();
  }
}
