#pragma once

#include <stddef.h>
#include <stdint.h>

#include "telemetry_types.h"
#include "vitals.h"

// A telemetry output (USB serial, BLE, ...). All calls come from the
// telemetry task, never from the acquisition or DSP tasks.
class TelemetrySink {
 public:
  virtual ~TelemetrySink() = default;
  virtual bool begin() = 0;
  virtual void publish_vitals(const vitals_t &v) {}
  virtual void publish_beat(const beat_event_t &b) {}
  virtual void publish_raw(const raw_record_t &r) {}
  virtual void publish_status(const telemetry_status_t &s) {}
  // Lets the DSP task skip producing raw records nobody consumes.
  virtual bool wants_raw() const { return false; }
};

class TelemetryFanout {
 public:
  static constexpr size_t kMaxSinks = 4;

  bool add(TelemetrySink *sink)
  {
    if (count_ >= kMaxSinks) {
      return false;
    }
    sinks_[count_++] = sink;
    return true;
  }

  // Returns false if any sink failed to start (the others keep running).
  bool begin()
  {
    bool ok = true;
    for (size_t i = 0; i < count_; i++) {
      ok &= sinks_[i]->begin();
    }
    return ok;
  }

  bool wants_raw() const
  {
    for (size_t i = 0; i < count_; i++) {
      if (sinks_[i]->wants_raw()) {
        return true;
      }
    }
    return false;
  }

  void publish_vitals(const vitals_t &v)
  {
    for (size_t i = 0; i < count_; i++) sinks_[i]->publish_vitals(v);
  }
  void publish_beat(const beat_event_t &b)
  {
    for (size_t i = 0; i < count_; i++) sinks_[i]->publish_beat(b);
  }
  void publish_raw(const raw_record_t &r)
  {
    for (size_t i = 0; i < count_; i++) {
      if (sinks_[i]->wants_raw()) sinks_[i]->publish_raw(r);
    }
  }
  void publish_status(const telemetry_status_t &s)
  {
    for (size_t i = 0; i < count_; i++) sinks_[i]->publish_status(s);
  }

 private:
  TelemetrySink *sinks_[kMaxSinks] = {};
  size_t count_ = 0;
};
