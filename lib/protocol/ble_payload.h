#pragma once

// Byte-exact encoders for every BLE characteristic. Plain C with no
// Arduino/Bluefruit dependency so they are covered by host tests.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "telemetry_types.h"
#include "vitals.h"

#ifdef __cplusplus
extern "C" {
#endif

// IEEE 11073-20601 16-bit SFLOAT: 4-bit signed exponent, 12-bit signed
// mantissa. Non-finite values and out-of-range values encode as NaN.
#define SFLOAT_NAN 0x07FF
uint16_t sfloat_encode(float value, int8_t exponent);

// Heart Rate Measurement (0x2A37).
#define BLE_HRM_MAX_LEN 20
#define BLE_HRM_MAX_RR 4
uint16_t ble_rr_to_1024(uint32_t rr_us);
size_t ble_hrm_encode(uint8_t *out, uint16_t hr_bpm, bool contact, const uint16_t *rr_1024,
                      uint8_t n_rr);

// PLX Continuous Measurement (0x2A5F): flags + SpO2PR-Normal (two SFLOATs).
#define BLE_PLX_CONT_LEN 5
size_t ble_plx_continuous_encode(uint8_t *out, const vitals_t *v);

// PLX Features (0x2A60): no optional features.
#define BLE_PLX_FEATURES_LEN 2
size_t ble_plx_features_encode(uint8_t *out);

// Custom beat/PAT characteristic: r_peak_t_us, rr_us, pat_foot_us,
// pat_peak_us (uint32 LE each) and flags (uint8).
#define BLE_BEAT_LEN 17
size_t ble_beat_encode(uint8_t *out, const beat_event_t *b);

// Custom status characteristic: flags (u8), ecg_fifo_ovf, ppg_fifo_ovf,
// rb_ovf, gaps (u32 LE each), i2c_errors (u16 LE).
#define BLE_STATUS_LEN 19
size_t ble_status_encode(uint8_t *out, const telemetry_status_t *s);

// Custom raw characteristic: count (u8) then `count` records of
// timestamp_us, ecg, red, ir (32-bit LE each).
#define BLE_RAW_RECORD_LEN 16
size_t ble_raw_records_per_packet(uint16_t att_payload);
size_t ble_raw_encode(uint8_t *out, const raw_record_t *records, uint8_t count);

#ifdef __cplusplus
}
#endif
