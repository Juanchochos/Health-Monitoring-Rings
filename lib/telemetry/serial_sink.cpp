#include "serial_sink.h"

#include <Arduino.h>

#include "app_config.h"

namespace {

// Without an open host port (DTR low) output is dropped instead of queued.
bool host_connected()
{
  return (bool)Serial;
}

}  // namespace

bool SerialSink::begin()
{
  if (mode_ == Mode::kRaw) {
#if !CSV_PLOTTER_MODE
    Serial.println("timestamp_us,ecg,red,ir");
#endif
  } else {
    Serial.println("# V,t_us,hr_bpm,pr_bpm,spo2_pct,r_ratio,pat_foot_us,pat_peak_us,flags");
    Serial.println("# B,r_peak_t_us,rr_us,pat_foot_us,pat_peak_us,flags");
  }
  return true;
}

void SerialSink::publish_vitals(const vitals_t &v)
{
  if (!host_connected()) {
    return;
  }
  Serial.printf("%sV,%lu,%.1f,%.1f,%.1f,%.4f,%lu,%lu,0x%02X\n", mode_ == Mode::kRaw ? "# " : "",
                (unsigned long)v.timestamp_us, v.hr_bpm, v.pr_bpm, v.spo2_pct, v.r_ratio,
                (unsigned long)v.pat_foot_us, (unsigned long)v.pat_peak_us, v.flags);
}

void SerialSink::publish_beat(const beat_event_t &b)
{
  if (!host_connected()) {
    return;
  }
  Serial.printf("%sB,%lu,%lu,%lu,%lu,0x%02X\n", mode_ == Mode::kRaw ? "# " : "",
                (unsigned long)b.r_peak_t_us, (unsigned long)b.rr_us,
                (unsigned long)b.pat_foot_us, (unsigned long)b.pat_peak_us, b.flags);
}

void SerialSink::publish_raw(const raw_record_t &r)
{
  if (!host_connected()) {
    return;
  }
#if CSV_PLOTTER_MODE
  Serial.printf("%ld,%ld,%ld\n", (long)r.ecg, (long)r.red, (long)r.ir);
#else
  Serial.printf("%lu,%ld,%ld,%ld\n", (unsigned long)r.timestamp_us, (long)r.ecg, (long)r.red,
                (long)r.ir);
#endif
}

void SerialSink::publish_status(const telemetry_status_t &s)
{
#if CSV_PLOTTER_MODE
  if (mode_ == Mode::kRaw) {
    return;
  }
#endif
  if (!host_connected()) {
    return;
  }
  Serial.printf(
      "# status flags=0x%02X ecg=%.1fHz ppg=%.1fHz ecg_fifo_ovf=%lu ppg_fifo_ovf=%lu "
      "rb_ovf=%lu gaps=%lu i2c_err=%u\n",
      s.flags, s.ecg_rate_hz, s.ppg_rate_hz, (unsigned long)s.ecg_fifo_overflows,
      (unsigned long)s.ppg_fifo_overflows, (unsigned long)s.rb_overflows,
      (unsigned long)s.gaps, s.i2c_errors);
}
