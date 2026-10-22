#pragma once

#include "telemetry_sink.h"

// USB CDC serial output.
//
// Vitals mode:
//   V,t_us,hr_bpm,pr_bpm,spo2_pct,r_ratio,pat_foot_us,pat_peak_us,flags   (1 Hz)
//   B,r_peak_t_us,rr_us,pat_foot_us,pat_peak_us,flags                    (per beat)
// Raw mode: the Phase 2/3 "timestamp_us,ecg,red,ir" CSV, with vitals and
// beats as '#' comment lines so tools/plot_serial.py still parses it.
// Status is always a '#' line (tools/serial_logger.py diverts those to .log).
class SerialSink : public TelemetrySink {
 public:
  enum class Mode { kVitals, kRaw };

  explicit SerialSink(Mode mode) : mode_(mode) {}

  bool begin() override;
  void publish_vitals(const vitals_t &v) override;
  void publish_beat(const beat_event_t &b) override;
  void publish_raw(const raw_record_t &r) override;
  void publish_status(const telemetry_status_t &s) override;
  bool wants_raw() const override { return mode_ == Mode::kRaw; }

 private:
  Mode mode_;
};
