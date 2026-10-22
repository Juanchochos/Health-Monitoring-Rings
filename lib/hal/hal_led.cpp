#include "hal_led.h"

#include <Arduino.h>

#include "board_config.h"

void hal_led_init(void)
{
  pinMode(PIN_STATUS_LED, OUTPUT);
  hal_led_set(false);
}

void hal_led_set(bool on)
{
  digitalWrite(PIN_STATUS_LED, on ? STATUS_LED_ON_LEVEL : !STATUS_LED_ON_LEVEL);
}

void hal_led_blink_code(uint8_t count)
{
  for (uint8_t i = 0; i < count; i++) {
    hal_led_set(true);
    delay(150);
    hal_led_set(false);
    delay(250);
  }
  delay(1200);
}

void hal_led_halt_with_code(uint8_t count)
{
  for (;;) {
    hal_led_blink_code(count);
  }
}
