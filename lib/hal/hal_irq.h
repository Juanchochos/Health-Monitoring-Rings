#pragma once

#include <stdint.h>

// Falling-edge interrupts with hardware timestamps. The GPIOTE IN event is
// wired through PPI to a TIMER capture, so the timestamp is latched by
// hardware at the edge; the callback then runs in the GPIOTE ISR (priority 3,
// FreeRTOS FromISR calls allowed) with that captured time.

typedef void (*hal_irq_edge_cb_t)(uint32_t t_edge_us);

#define HAL_IRQ_MAX_SLOTS 2

// slot: 0..HAL_IRQ_MAX_SLOTS-1, one per interrupt line.
bool hal_irq_attach_timestamped(uint8_t slot, uint8_t pin, uint8_t ppi_ch, uint8_t cc,
                                hal_irq_edge_cb_t cb);

bool hal_irq_pin_is_low(uint8_t pin);
