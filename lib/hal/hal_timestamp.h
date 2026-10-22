#pragma once

#include <stdint.h>

// Free-running 32-bit, 1 MHz hardware timebase on TS_TIMER. Wraps every
// ~71.6 min; compare timestamps with unsigned subtraction only.

void hal_timestamp_init(void);

// Current time. Task context only (uses a FreeRTOS critical section).
uint32_t hal_timestamp_now_us(void);

// Value latched into capture register `cc` by a PPI-routed event.
uint32_t hal_timestamp_captured_us(uint8_t cc);

// Route a peripheral event to TS_TIMER->TASKS_CAPTURE[cc] through PPI channel
// ppi_ch, via the SoftDevice API when the SoftDevice is enabled.
bool hal_timestamp_route_event(uint8_t ppi_ch, volatile uint32_t *event, uint8_t cc);
