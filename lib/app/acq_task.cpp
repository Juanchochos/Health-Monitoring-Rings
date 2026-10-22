#include "acq_task.h"

#include "app_config.h"
#include "board_config.h"
#include "hal_irq.h"
#include "hal_timestamp.h"
#include "max30003/max30003.h"
#include "max30102/max30102.h"
#include "timestamp_recon.h"

namespace {

constexpr uint32_t kNotifyEcg = 1u << 0;
constexpr uint32_t kNotifyPpg = 1u << 1;
constexpr uint8_t kSlotEcg = 0;
constexpr uint8_t kSlotPpg = 1;
constexpr uint8_t kMaxStuckRetries = 3;
constexpr uint32_t kTaskStackWords = 1024;

constexpr uint32_t kEcgIrqIntervalMs = 1000UL * ECG_FIFO_THRESHOLD / ECG_SAMPLE_RATE_HZ;
constexpr uint32_t kPpgIrqIntervalMs = 1000UL * PPG_FIFO_THRESHOLD / PPG_SAMPLE_RATE_HZ;
constexpr uint32_t kEcgFallbackMs = 2 * kEcgIrqIntervalMs;
constexpr uint32_t kPpgFallbackMs = 2 * kPpgIrqIntervalMs;
constexpr uint32_t kWaitMs = kEcgFallbackMs < kPpgFallbackMs ? kEcgFallbackMs : kPpgFallbackMs;

sample_t g_ecg_storage[RB_ECG_CAPACITY];
sample_t g_red_storage[RB_PPG_CAPACITY];
sample_t g_ir_storage[RB_PPG_CAPACITY];
ring_buffer_t g_rb_ecg;
ring_buffer_t g_rb_red;
ring_buffer_t g_rb_ir;

ts_recon_t g_ecg_recon;
ts_recon_t g_ppg_recon;

TaskHandle_t g_task = nullptr;
TaskHandle_t g_consumer = nullptr;

volatile uint32_t g_ecg_edge_us = 0;
volatile uint32_t g_ppg_edge_us = 0;
volatile uint32_t g_ecg_edge_count = 0;
volatile uint32_t g_ppg_edge_count = 0;
uint32_t g_ecg_edges_serviced = 0;
uint32_t g_ppg_edges_serviced = 0;

volatile bool g_ecg_contact = true;
acq_stats_t g_stats = {};

uint32_t g_ecg_last_t = 0;
bool g_ecg_have_last = false;
uint32_t g_ppg_last_t = 0;
bool g_ppg_have_last = false;
TickType_t g_ecg_last_service = 0;
TickType_t g_ppg_last_service = 0;

void on_ecg_edge(uint32_t t_edge_us)
{
  g_ecg_edge_us = t_edge_us;
  g_ecg_edge_count = g_ecg_edge_count + 1;
  BaseType_t woken = pdFALSE;
  xTaskNotifyFromISR(g_task, kNotifyEcg, eSetBits, &woken);
  portYIELD_FROM_ISR(woken);
}

void on_ppg_edge(uint32_t t_edge_us)
{
  g_ppg_edge_us = t_edge_us;
  g_ppg_edge_count = g_ppg_edge_count + 1;
  BaseType_t woken = pdFALSE;
  xTaskNotifyFromISR(g_task, kNotifyPpg, eSetBits, &woken);
  portYIELD_FROM_ISR(woken);
}

void count_gap(uint32_t t, uint32_t *last, bool *have_last, float period_us, uint32_t *gaps)
{
  if (*have_last) {
    float dt = (float)(int32_t)(t - *last);
    if (dt > 1.5f * period_us || dt < 0.5f * period_us) {
      (*gaps)++;
    }
  }
  *last = t;
  *have_last = true;
}

// Edge time to use for this batch, or false if the edge cannot be trusted:
// no new edge, or more than one edge since the last service (a batch was
// missed, so the "threshold-th sample" alignment no longer holds).
bool take_edge(volatile uint32_t *edge_count, uint32_t *serviced, volatile uint32_t *edge_us,
               uint32_t *t_edge)
{
  taskENTER_CRITICAL();
  uint32_t count = *edge_count;
  *t_edge = *edge_us;
  taskEXIT_CRITICAL();
  uint32_t new_edges = count - *serviced;
  *serviced = count;
  return new_edges == 1;
}

bool service_ecg(bool notified)
{
  uint32_t t_edge = 0;
  bool edge = notified && take_edge(&g_ecg_edge_count, &g_ecg_edges_serviced, &g_ecg_edge_us,
                                    &t_edge);
  uint32_t t_start = hal_timestamp_now_us();
  if (edge) {
    uint32_t latency = t_start - t_edge;
    g_stats.ecg_edges++;
    g_stats.ecg_latency_sum_us += latency;
    if (latency > g_stats.ecg_latency_max_us) {
      g_stats.ecg_latency_max_us = latency;
    }
  }

  uint32_t status = max30003_read_status();
  if (ECG_DCLOFF_IMAG_NA > 0) {
    g_ecg_contact = !max30003_status_lead_off(status);
  }

  int32_t raw[MAX30003_FIFO_DEPTH];
  max30003_fifo_result_t res = max30003_read_fifo(raw, MAX30003_FIFO_DEPTH);
  if (res.overflow || (status & MAX30003_STATUS_EOVF)) {
    max30003_fifo_reset();
    g_stats.ecg_fifo_overflows++;
    ts_recon_reset(&g_ecg_recon);
    g_ecg_have_last = false;
    edge = false;
  }
  g_ecg_last_service = xTaskGetTickCount();
  if (res.count == 0) {
    return false;
  }

  uint32_t t[MAX30003_FIFO_DEPTH];
  if (edge && res.count >= ECG_FIFO_THRESHOLD) {
    ts_recon_batch_edge(&g_ecg_recon, t_edge, res.count, t);
  } else {
    g_stats.ecg_polled_reads++;
    ts_recon_batch_polled(&g_ecg_recon, t_start, res.count, t);
  }

  const float period = g_ecg_recon.ts_nominal_us;
  for (uint8_t i = 0; i < res.count; i++) {
    count_gap(t[i], &g_ecg_last_t, &g_ecg_have_last, period, &g_stats.ecg_gaps);
    sample_t s = {t[i], raw[i]};
    rb_push(&g_rb_ecg, &s);
  }
  g_stats.ecg_samples += res.count;
  return true;
}

bool service_ppg(bool notified)
{
  uint32_t t_edge = 0;
  bool edge = notified && take_edge(&g_ppg_edge_count, &g_ppg_edges_serviced, &g_ppg_edge_us,
                                    &t_edge);
  uint32_t t_start = hal_timestamp_now_us();
  if (edge) {
    uint32_t latency = t_start - t_edge;
    g_stats.ppg_edges++;
    g_stats.ppg_latency_sum_us += latency;
    if (latency > g_stats.ppg_latency_max_us) {
      g_stats.ppg_latency_max_us = latency;
    }
  }

  uint8_t int_status = 0;
  if (!max30102_read_int_status(&int_status)) {
    g_stats.i2c_errors++;
  }

  uint32_t red[MAX30102_FIFO_DEPTH], ir[MAX30102_FIFO_DEPTH];
  max30102_fifo_result_t res = max30102_read_fifo(red, ir, MAX30102_FIFO_DEPTH);
  g_ppg_last_service = xTaskGetTickCount();
  if (!res.ok) {
    g_stats.i2c_errors++;
    return false;
  }
  if (res.overflow) {
    g_stats.ppg_fifo_overflows += res.overflow;
    ts_recon_reset(&g_ppg_recon);
    g_ppg_have_last = false;
    edge = false;
  }
  if (res.count == 0) {
    return false;
  }

  uint32_t t[MAX30102_FIFO_DEPTH];
  if (edge && res.count >= PPG_FIFO_THRESHOLD) {
    ts_recon_batch_edge(&g_ppg_recon, t_edge, res.count, t);
  } else {
    g_stats.ppg_polled_reads++;
    ts_recon_batch_polled(&g_ppg_recon, t_start, res.count, t);
  }

  const float period = g_ppg_recon.ts_nominal_us;
  for (uint8_t i = 0; i < res.count; i++) {
    count_gap(t[i], &g_ppg_last_t, &g_ppg_have_last, period, &g_stats.ppg_gaps);
    sample_t s_red = {t[i], (int32_t)red[i]};
    sample_t s_ir = {t[i], (int32_t)ir[i]};
    rb_push(&g_rb_red, &s_red);
    rb_push(&g_rb_ir, &s_ir);
  }
  g_stats.ppg_samples += res.count;
  return true;
}

bool overdue(TickType_t last, uint32_t limit_ms)
{
  return (xTaskGetTickCount() - last) >= pdMS_TO_TICKS(limit_ms);
}

void acq_task(void *)
{
  for (;;) {
    uint32_t bits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(kWaitMs));

    bool pushed = false;
    if ((bits & kNotifyEcg) || overdue(g_ecg_last_service, kEcgFallbackMs)) {
      pushed |= service_ecg(bits & kNotifyEcg);
    }
    if ((bits & kNotifyPpg) || overdue(g_ppg_last_service, kPpgFallbackMs)) {
      pushed |= service_ppg(bits & kNotifyPpg);
    }

    // A line still low after servicing will never produce another falling
    // edge; drain again until it releases.
    for (uint8_t i = 0; i < kMaxStuckRetries && hal_irq_pin_is_low(PIN_ECG_INTB); i++) {
      pushed |= service_ecg(false);
    }
    for (uint8_t i = 0; i < kMaxStuckRetries && hal_irq_pin_is_low(PIN_PPG_INT); i++) {
      pushed |= service_ppg(false);
    }

    if (pushed && g_consumer != nullptr) {
      xTaskNotifyGive(g_consumer);
    }
  }
}

}  // namespace

void acq_init()
{
  rb_init(&g_rb_ecg, g_ecg_storage, RB_ECG_CAPACITY);
  rb_init(&g_rb_red, g_red_storage, RB_PPG_CAPACITY);
  rb_init(&g_rb_ir, g_ir_storage, RB_PPG_CAPACITY);
  ts_recon_init(&g_ecg_recon, ECG_SAMPLE_RATE_HZ, ECG_FIFO_THRESHOLD, ECG_AFE_LATENCY_US,
                TS_RECON_EMA_ALPHA, TS_RECON_MAX_DEVIATION);
  ts_recon_init(&g_ppg_recon, PPG_SAMPLE_RATE_HZ, PPG_FIFO_THRESHOLD, PPG_AFE_LATENCY_US,
                TS_RECON_EMA_ALPHA, TS_RECON_MAX_DEVIATION);
}

bool acq_start(TaskHandle_t consumer)
{
  g_consumer = consumer;
  if (xTaskCreate(acq_task, "acq", kTaskStackWords, nullptr, TASK_PRIO_HIGH, &g_task) != pdPASS) {
    return false;
  }
  bool ok = hal_irq_attach_timestamped(kSlotEcg, PIN_ECG_INTB, TS_PPI_CH_ECG, TS_CC_ECG,
                                       on_ecg_edge) &&
            hal_irq_attach_timestamped(kSlotPpg, PIN_PPG_INT, TS_PPI_CH_PPG, TS_CC_PPG,
                                       on_ppg_edge);
  // Lines may already be low from samples queued before the interrupts were
  // attached; the stuck-line check in the task drains them on its first pass.
  xTaskNotify(g_task, 0, eNoAction);
  return ok;
}

ring_buffer_t *acq_rb_ecg() { return &g_rb_ecg; }
ring_buffer_t *acq_rb_ppg_red() { return &g_rb_red; }
ring_buffer_t *acq_rb_ppg_ir() { return &g_rb_ir; }

bool acq_ecg_contact() { return g_ecg_contact; }

void acq_get_stats(acq_stats_t *out)
{
  *out = g_stats;
  out->ecg_rb_overflows = rb_overflows(&g_rb_ecg);
  out->ppg_rb_overflows = rb_overflows(&g_rb_red);
  out->ecg_period_us = ts_recon_period_us(&g_ecg_recon);
  out->ppg_period_us = ts_recon_period_us(&g_ppg_recon);
}
