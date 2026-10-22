#pragma once

#include <stdint.h>

void hal_led_init(void);
void hal_led_set(bool on);

// Play `count` short blinks followed by a long pause, once.
void hal_led_blink_code(uint8_t count);

// Repeat the blink code forever; used for fatal bring-up failures.
[[noreturn]] void hal_led_halt_with_code(uint8_t count);
