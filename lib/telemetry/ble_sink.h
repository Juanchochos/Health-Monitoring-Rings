#pragma once

#include "ble_payload.h"
#include "telemetry_sink.h"

// Bluefruit GATT server:
//   Heart Rate (0x180D): Measurement 0x2A37 (notify, contact bits + RR),
//                        Body Sensor Location 0x2A38 = finger.
//   Pulse Oximeter (0x1822): PLX Continuous 0x2A5F (notify, SpO2 + PR SFLOAT),
//                            PLX Features 0x2A60.
//   Custom service 4f9a0001-7c1e-4b6d-9a2f-3e8c5d1b7a60:
//     ...0002 beat/PAT (read, notify), ...0003 status (read, notify),
//     ...0004 raw waveform (notify; only when raw streaming is enabled).
//
// begin() starts the SoftDevice, so it must run before hal_timestamp_init():
// once the SoftDevice is up, PPI is configured through sd_ppi_* calls.
class BleSink : public TelemetrySink {
 public:
  explicit BleSink(bool raw_enabled) : raw_enabled_(raw_enabled) {}

  bool begin() override;
  void publish_vitals(const vitals_t &v) override;
  void publish_beat(const beat_event_t &b) override;
  void publish_raw(const raw_record_t &r) override;
  void publish_status(const telemetry_status_t &s) override;
  bool wants_raw() const override;

 private:
  void notify_hrm(uint16_t hr_bpm, bool contact, uint32_t rr_us);

  bool raw_enabled_;
  vitals_t last_vitals_ = {};
  uint32_t last_hrm_ms_ = 0;
  static constexpr size_t kRawMaxRecords = 15;
  raw_record_t raw_pending_[kRawMaxRecords] = {};
  uint8_t raw_count_ = 0;
};
