#include "dsp_task.h"

#include "acq_task.h"
#include "app_config.h"
#include "dsp_pipeline.h"
#include "ring_buffer.h"

namespace {

constexpr uint32_t kTaskStackWords = 1024;
constexpr uint32_t kIdleWaitMs = 100;
constexpr UBaseType_t kBeatQueueLen = 8;
constexpr UBaseType_t kRawQueueLen = 128;

// Each stream waits for the other to catch up before being consumed, unless
// this much of it has queued (the other sensor stalled or absent).
constexpr uint32_t kEcgHoldback = ECG_SAMPLE_RATE_HZ * 2 / 5;
constexpr uint32_t kPpgHoldback = PPG_SAMPLE_RATE_HZ / 2;

dsp_pipeline_t g_dsp;
TaskHandle_t g_task = nullptr;
TaskHandle_t g_telemetry = nullptr;
QueueHandle_t g_q_vitals = nullptr;
QueueHandle_t g_q_beats = nullptr;
QueueHandle_t g_q_raw = nullptr;
volatile bool g_raw_enabled = false;
volatile uint32_t g_dropped = 0;

int32_t g_held_red = 0;
int32_t g_held_ir = 0;
uint32_t g_last_ecg_t = 0;

dsp_config_t app_dsp_config()
{
  dsp_config_t c = {};
  c.ecg_fs = ECG_SAMPLE_RATE_HZ;
  c.ecg_filter.fs = ECG_SAMPLE_RATE_HZ;
  c.ecg_filter.bandpass_enable = ECG_SW_BANDPASS_ENABLE;
  c.ecg_filter.hpf_hz = ECG_HPF_HZ;
  c.ecg_filter.lpf_hz = ECG_LPF_HZ;
  c.ecg_filter.notch_enable = ECG_NOTCH_ENABLE;
  c.ecg_filter.notch_hz = ECG_NOTCH_HZ;
  c.ecg_filter.notch_q = ECG_NOTCH_Q;
  c.ecg_filter.delay_ref_hz = ECG_DELAY_REF_HZ;
  c.ppg_fs = PPG_SAMPLE_RATE_HZ;
  c.ppg_dc_lpf_hz = PPG_DC_LPF_HZ;
  c.ppg_bp_lo_hz = PPG_BP_LO_HZ;
  c.ppg_bp_hi_hz = PPG_BP_HI_HZ;
  c.ppg_timing_lpf_hz = PPG_TIMING_LPF_HZ;
  c.ppg_delay_ref_hz = PPG_DELAY_REF_HZ;
  c.ppg_finger_dc_min = PPG_FINGER_DC_MIN;
  c.pat_min_us = PAT_MIN_US;
  c.pat_max_us = PAT_MAX_US;
  c.spo2_a = SPO2_COEF_A;
  c.spo2_b = SPO2_COEF_B;
  c.spo2_c = SPO2_COEF_C;
  c.warmup_us = WARMUP_US;
  c.warmup_beats = WARMUP_BEATS;
  return c;
}

void push_ppg_pair()
{
  sample_t red, ir;
  rb_pop(acq_rb_ppg_red(), &red);
  rb_pop(acq_rb_ppg_ir(), &ir);
  g_held_red = red.raw_sample;
  g_held_ir = ir.raw_sample;
  dsp_pipeline_push_ppg(&g_dsp, &red, &ir);
}

void push_ecg(const sample_t &ecg)
{
  g_last_ecg_t = ecg.timestamp_us;
  dsp_pipeline_push_ecg(&g_dsp, &ecg);
  if (g_raw_enabled) {
    const raw_record_t r = {ecg.timestamp_us, ecg.raw_sample, g_held_red, g_held_ir};
    if (xQueueSend(g_q_raw, &r, 0) != pdTRUE) {
      g_dropped++;
    }
  }
}

// Consumes both streams in timestamp order. Returns true if anything ran.
bool drain()
{
  ring_buffer_t *rb_ecg = acq_rb_ecg();
  ring_buffer_t *rb_red = acq_rb_ppg_red();
  bool any = false;
  sample_t ecg, red;

  for (;;) {
    const bool have_ecg = rb_peek(rb_ecg, &ecg);
    const bool have_ppg = rb_peek(rb_red, &red);
    if (have_ecg && have_ppg) {
      if ((int32_t)(red.timestamp_us - ecg.timestamp_us) <= 0) {
        push_ppg_pair();
      } else {
        rb_pop(rb_ecg, &ecg);
        push_ecg(ecg);
      }
    } else if (have_ecg && rb_count(rb_ecg) >= kEcgHoldback) {
      rb_pop(rb_ecg, &ecg);
      push_ecg(ecg);
    } else if (have_ppg && rb_count(rb_red) >= kPpgHoldback) {
      push_ppg_pair();
    } else {
      break;
    }
    any = true;
  }
  return any;
}

void dsp_task(void *)
{
  uint32_t last_vitals_ms = millis();
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kIdleWaitMs));

    dsp_pipeline_set_ecg_contact(&g_dsp, acq_ecg_contact(), g_last_ecg_t);
    bool output = drain() && g_raw_enabled;

    beat_event_t beat;
    while (dsp_pipeline_pop_beat(&g_dsp, &beat)) {
      if (xQueueSend(g_q_beats, &beat, 0) != pdTRUE) {
        g_dropped++;
      }
      output = true;
    }

    if (millis() - last_vitals_ms >= VITALS_PERIOD_MS) {
      last_vitals_ms += VITALS_PERIOD_MS;
      vitals_t v;
      dsp_pipeline_get_vitals(&g_dsp, &v);
      xQueueOverwrite(g_q_vitals, &v);
      output = true;
    }

    if (output && g_telemetry) {
      xTaskNotifyGive(g_telemetry);
    }
  }
}

}  // namespace

bool dsp_task_start(TaskHandle_t telemetry_task)
{
  g_telemetry = telemetry_task;
  const dsp_config_t cfg = app_dsp_config();
  dsp_pipeline_init(&g_dsp, &cfg);

  g_q_vitals = xQueueCreate(1, sizeof(vitals_t));
  g_q_beats = xQueueCreate(kBeatQueueLen, sizeof(beat_event_t));
  g_q_raw = xQueueCreate(kRawQueueLen, sizeof(raw_record_t));
  if (!g_q_vitals || !g_q_beats || !g_q_raw) {
    return false;
  }
  return xTaskCreate(dsp_task, "dsp", kTaskStackWords, nullptr, TASK_PRIO_NORMAL, &g_task) ==
         pdPASS;
}

TaskHandle_t dsp_task_handle() { return g_task; }

void dsp_task_set_raw_enabled(bool enabled) { g_raw_enabled = enabled; }

bool dsp_take_vitals(vitals_t *out) { return xQueueReceive(g_q_vitals, out, 0) == pdTRUE; }

bool dsp_take_beat(beat_event_t *out) { return xQueueReceive(g_q_beats, out, 0) == pdTRUE; }

bool dsp_take_raw(raw_record_t *out) { return xQueueReceive(g_q_raw, out, 0) == pdTRUE; }

uint32_t dsp_dropped_outputs() { return g_dropped; }
