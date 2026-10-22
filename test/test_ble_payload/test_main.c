#include <math.h>
#include <stdint.h>
#include <unity.h>

#include "ble_payload.h"

void setUp(void) {}
void tearDown(void) {}

static void test_sfloat(void)
{
  TEST_ASSERT_EQUAL_HEX16(0xF3CF, sfloat_encode(97.5f, -1));   // 975 x 10^-1
  TEST_ASSERT_EQUAL_HEX16(0xF2BC, sfloat_encode(70.0f, -1));   // 700 x 10^-1
  TEST_ASSERT_EQUAL_HEX16(0x0048, sfloat_encode(72.0f, 0));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF & (0xF000 | (uint16_t)(-5 & 0x0FFF)), sfloat_encode(-0.5f, -1));
  TEST_ASSERT_EQUAL_HEX16(SFLOAT_NAN, sfloat_encode(NAN, -1));
  TEST_ASSERT_EQUAL_HEX16(SFLOAT_NAN, sfloat_encode(250.0f, -1));  // 2500 > 2045
}

static void test_rr_units(void)
{
  TEST_ASSERT_EQUAL_UINT16(1024, ble_rr_to_1024(1000000u));
  TEST_ASSERT_EQUAL_UINT16(819, ble_rr_to_1024(800000u));     // 819.2
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, ble_rr_to_1024(100000000u));
}

static void test_hrm(void)
{
  uint8_t buf[BLE_HRM_MAX_LEN];
  const uint16_t rr[] = {819, 830};

  size_t n = ble_hrm_encode(buf, 72, true, rr, 2);
  TEST_ASSERT_EQUAL(6, n);
  const uint8_t expect[] = {0x16, 72, 0x33, 0x03, 0x3E, 0x03};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, 6);

  n = ble_hrm_encode(buf, 0, false, NULL, 0);
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_EQUAL_HEX8(0x04, buf[0]);

  n = ble_hrm_encode(buf, 300, true, NULL, 0);
  TEST_ASSERT_EQUAL(3, n);
  TEST_ASSERT_EQUAL_HEX8(0x07, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x2C, buf[1]);
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[2]);
}

static void test_plx_continuous(void)
{
  uint8_t buf[BLE_PLX_CONT_LEN];
  vitals_t v = {0};
  v.spo2_pct = 97.5f;
  v.pr_bpm = 70.0f;
  v.flags = VITALS_FLAG_SPO2_VALID | VITALS_FLAG_PR_VALID;
  TEST_ASSERT_EQUAL(BLE_PLX_CONT_LEN, ble_plx_continuous_encode(buf, &v));
  const uint8_t expect[] = {0x00, 0xCF, 0xF3, 0xBC, 0xF2};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, 5);

  v.flags = VITALS_FLAG_PR_VALID;
  ble_plx_continuous_encode(buf, &v);
  TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
  TEST_ASSERT_EQUAL_HEX8(0x07, buf[2]);
}

static void test_beat_and_status(void)
{
  uint8_t buf[32];
  beat_event_t b = {0x11223344u, 800000u, 160000u, 250000u, 0x3F};
  TEST_ASSERT_EQUAL(BLE_BEAT_LEN, ble_beat_encode(buf, &b));
  TEST_ASSERT_EQUAL_HEX8(0x44, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x11, buf[3]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[4]);   // 800000 = 0x000C3500
  TEST_ASSERT_EQUAL_HEX8(0x35, buf[5]);
  TEST_ASSERT_EQUAL_HEX8(0x3F, buf[16]);

  telemetry_status_t s = {0x03, 1, 2, 3, 4, 0x0506, 256.0f, 100.0f};
  TEST_ASSERT_EQUAL(BLE_STATUS_LEN, ble_status_encode(buf, &s));
  TEST_ASSERT_EQUAL_HEX8(0x03, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);
  TEST_ASSERT_EQUAL_HEX8(0x04, buf[13]);
  TEST_ASSERT_EQUAL_HEX8(0x06, buf[17]);
  TEST_ASSERT_EQUAL_HEX8(0x05, buf[18]);
}

static void test_raw_packets(void)
{
  TEST_ASSERT_EQUAL(1, ble_raw_records_per_packet(20));     // default MTU 23
  TEST_ASSERT_EQUAL(15, ble_raw_records_per_packet(244));   // MTU 247
  raw_record_t r[2] = {{1000u, -5, 100, 200}, {4906u, 7, 101, 201}};
  uint8_t buf[1 + 2 * BLE_RAW_RECORD_LEN];
  TEST_ASSERT_EQUAL(sizeof(buf), ble_raw_encode(buf, r, 2));
  TEST_ASSERT_EQUAL_HEX8(2, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFB, buf[5]);   // -5 little-endian
  TEST_ASSERT_EQUAL_HEX8(0xFF, buf[8]);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_sfloat);
  RUN_TEST(test_rr_units);
  RUN_TEST(test_hrm);
  RUN_TEST(test_plx_continuous);
  RUN_TEST(test_beat_and_status);
  RUN_TEST(test_raw_packets);
  return UNITY_END();
}
