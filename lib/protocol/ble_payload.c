#include "ble_payload.h"

#include <math.h>

#define HRM_FLAG_HR_U16          0x01
#define HRM_FLAG_CONTACT_DETECT  0x02
#define HRM_FLAG_CONTACT_SUPPORT 0x04
#define HRM_FLAG_RR_PRESENT      0x10

static uint8_t *put_u16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  return p + 2;
}

static uint8_t *put_u32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
  return p + 4;
}

uint16_t sfloat_encode(float value, int8_t exponent)
{
  if (!isfinite(value) || exponent < -8 || exponent > 7) {
    return SFLOAT_NAN;
  }
  const long mantissa = lroundf(value * powf(10.0f, (float)-exponent));
  // 0x07FE..0x0802 are reserved special values.
  if (mantissa > 2045 || mantissa < -2045) {
    return SFLOAT_NAN;
  }
  return (uint16_t)((((uint16_t)exponent & 0x0F) << 12) | ((uint16_t)mantissa & 0x0FFF));
}

uint16_t ble_rr_to_1024(uint32_t rr_us)
{
  const uint32_t v = (uint32_t)(((uint64_t)rr_us * 1024u + 500000u) / 1000000u);
  return v > 0xFFFF ? 0xFFFF : (uint16_t)v;
}

size_t ble_hrm_encode(uint8_t *out, uint16_t hr_bpm, bool contact, const uint16_t *rr_1024,
                      uint8_t n_rr)
{
  uint8_t flags = HRM_FLAG_CONTACT_SUPPORT | (contact ? HRM_FLAG_CONTACT_DETECT : 0);
  if (n_rr > BLE_HRM_MAX_RR) {
    n_rr = BLE_HRM_MAX_RR;
  }
  if (n_rr) {
    flags |= HRM_FLAG_RR_PRESENT;
  }
  uint8_t *p = out + 1;
  if (hr_bpm > 255) {
    flags |= HRM_FLAG_HR_U16;
    p = put_u16(p, hr_bpm);
  } else {
    *p++ = (uint8_t)hr_bpm;
  }
  for (uint8_t i = 0; i < n_rr; i++) {
    p = put_u16(p, rr_1024[i]);
  }
  out[0] = flags;
  return (size_t)(p - out);
}

size_t ble_plx_continuous_encode(uint8_t *out, const vitals_t *v)
{
  const bool spo2_ok = (v->flags & VITALS_FLAG_SPO2_VALID) != 0;
  const bool pr_ok = (v->flags & VITALS_FLAG_PR_VALID) != 0;
  uint8_t *p = out;
  *p++ = 0x00;  // no optional fields
  p = put_u16(p, spo2_ok ? sfloat_encode(v->spo2_pct, -1) : SFLOAT_NAN);
  p = put_u16(p, pr_ok ? sfloat_encode(v->pr_bpm, -1) : SFLOAT_NAN);
  return (size_t)(p - out);
}

size_t ble_plx_features_encode(uint8_t *out)
{
  put_u16(out, 0x0000);
  return BLE_PLX_FEATURES_LEN;
}

size_t ble_beat_encode(uint8_t *out, const beat_event_t *b)
{
  uint8_t *p = out;
  p = put_u32(p, b->r_peak_t_us);
  p = put_u32(p, b->rr_us);
  p = put_u32(p, b->pat_foot_us);
  p = put_u32(p, b->pat_peak_us);
  *p++ = b->flags;
  return (size_t)(p - out);
}

size_t ble_status_encode(uint8_t *out, const telemetry_status_t *s)
{
  uint8_t *p = out;
  *p++ = s->flags;
  p = put_u32(p, s->ecg_fifo_overflows);
  p = put_u32(p, s->ppg_fifo_overflows);
  p = put_u32(p, s->rb_overflows);
  p = put_u32(p, s->gaps);
  p = put_u16(p, s->i2c_errors);
  return (size_t)(p - out);
}

size_t ble_raw_records_per_packet(uint16_t att_payload)
{
  if (att_payload <= 1) {
    return 0;
  }
  const size_t n = (att_payload - 1u) / BLE_RAW_RECORD_LEN;
  return n > 255 ? 255 : n;
}

size_t ble_raw_encode(uint8_t *out, const raw_record_t *records, uint8_t count)
{
  uint8_t *p = out;
  *p++ = count;
  for (uint8_t i = 0; i < count; i++) {
    p = put_u32(p, records[i].timestamp_us);
    p = put_u32(p, (uint32_t)records[i].ecg);
    p = put_u32(p, (uint32_t)records[i].red);
    p = put_u32(p, (uint32_t)records[i].ir);
  }
  return (size_t)(p - out);
}
