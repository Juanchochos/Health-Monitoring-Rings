#pragma once

#include <Arduino.h>

#include "telemetry_types.h"
#include "vitals.h"

// DSP layer task: drains the acquisition ring buffers in timestamp order,
// runs the DSP pipeline, and hands results to the telemetry task through
// queues. The telemetry task gets a notification whenever output is ready.

bool dsp_task_start(TaskHandle_t telemetry_task);
TaskHandle_t dsp_task_handle();

// Raw records are only produced while some sink consumes them.
void dsp_task_set_raw_enabled(bool enabled);

bool dsp_take_vitals(vitals_t *out);   // newest snapshot, at VITALS_PERIOD_MS
bool dsp_take_beat(beat_event_t *out);
bool dsp_take_raw(raw_record_t *out);
uint32_t dsp_dropped_outputs();        // queue-full drops (telemetry too slow)
