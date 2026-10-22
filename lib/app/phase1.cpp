// Phase 1: bus and silicon sanity check.
// Reads MAX30102 PART_ID (expect 0x15) over I2C and MAX30003 INFO (expect a
// non-zero revision) over SPI, prints PASS/FAIL, and blinks a code:
//   slow single blink = both pass, 2 = PPG fail, 3 = ECG fail, 4 = both fail.
// Checks repeat every 5sec so wiring can be reseated while watching the monitor.

#include <Arduino.h>

#include "app.h"
#include "app_config.h"
#include "board_config.h"
#include "hal_i2c.h"
#include "hal_led.h"
#include "max30003/max30003.h"
#include "max30102/max30102.h"

namespace {

constexpr uint8_t kBlinkPass = 1;
constexpr uint8_t kBlinkPpgFail = 2;
constexpr uint8_t kBlinkEcgFail = 3;
constexpr uint8_t kBlinkBothFail = 4;
constexpr uint32_t kRecheckMs = 5000;

uint8_t g_blink_code = kBlinkBothFail;
uint32_t g_last_check_ms = 0;

void wait_for_serial(uint32_t timeout_ms)
{
  uint32_t start = millis();
  while (!Serial && millis() - start < timeout_ms) {
    delay(10);
  }
}

void i2c_scan()
{
  Serial.print("  I2C scan:");
  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    if (hal_i2c_probe(addr)) {
      Serial.printf(" 0x%02X", addr);
      found++;
    }
  }
  Serial.println(found ? "" : " no devices (check SDA/SCL wiring, pull-ups, power)");
}

bool check_ppg()
{
  uint8_t part = 0, rev = 0;
  bool bus_ok = max30102_read_part_id(&part, &rev);
  bool pass = bus_ok && part == MAX30102_PART_ID_VALUE;
  if (!bus_ok) {
    Serial.printf("[FAIL] MAX30102 @0x%02X: no I2C ACK\n", PPG_I2C_ADDR);
  } else {
    Serial.printf("[%s] MAX30102 PART_ID=0x%02X (expect 0x%02X) REV_ID=0x%02X\n",
                  pass ? "PASS" : "FAIL", part, MAX30102_PART_ID_VALUE, rev);
  }
  if (!pass) {
    i2c_scan();
  }
  return pass;
}

bool check_ecg()
{
  uint32_t info = max30003_read_info();
  bool pass = info != 0 && info != 0xFFFFFF;
  uint8_t pattern = (info >> 20) & 0xF;
  uint8_t revision = (info >> 16) & 0xF;
  Serial.printf("[%s] MAX30003 INFO=0x%06lX (revision %u)\n", pass ? "PASS" : "FAIL",
                (unsigned long)info, revision);
  if (!pass) {
    Serial.println(info == 0 ? "  MISO stuck low or chip unpowered"
                             : "  MISO stuck high: check CS (D10), SCK, MISO wiring");
  } else if (pattern != 0x5) {
    Serial.printf("  WARNING: INFO[23:20]=0x%X, datasheet fixed pattern is 0x5\n", pattern);
  }
  return pass;
}

void run_checks()
{
  Serial.println();
  Serial.println("=== Phase 1: bus and silicon sanity check ===");
  Serial.printf("Board: %s\n", BOARD_NAME);

  bool ppg_ok = check_ppg();
  bool ecg_ok = check_ecg();

  if (ppg_ok && ecg_ok) {
    g_blink_code = kBlinkPass;
  } else if (!ppg_ok && !ecg_ok) {
    g_blink_code = kBlinkBothFail;
  } else {
    g_blink_code = ppg_ok ? kBlinkEcgFail : kBlinkPpgFail;
  }
  Serial.printf("RESULT: %s\n", (ppg_ok && ecg_ok) ? "PASS" : "FAIL");
  g_last_check_ms = millis();
}

}  // namespace

void phase1_setup()
{
  Serial.begin(SERIAL_BAUD);
  wait_for_serial(3000);

  hal_led_init();
  max30003_init_bus();
  max30102_init_bus();

  run_checks();
}

void phase1_loop()
{
  hal_led_blink_code(g_blink_code);
  if (millis() - g_last_check_ms >= kRecheckMs) {
    run_checks();
  }
}
