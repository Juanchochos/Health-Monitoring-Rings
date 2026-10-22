#include "hal_irq.h"

#include <Arduino.h>

#include "hal_timestamp.h"

namespace {

struct Slot {
  uint8_t cc;
  hal_irq_edge_cb_t cb;
};

Slot g_slots[HAL_IRQ_MAX_SLOTS];

void trampoline0() { g_slots[0].cb(hal_timestamp_captured_us(g_slots[0].cc)); }
void trampoline1() { g_slots[1].cb(hal_timestamp_captured_us(g_slots[1].cc)); }

constexpr voidFuncPtr kTrampolines[HAL_IRQ_MAX_SLOTS] = {trampoline0, trampoline1};

}  // namespace

bool hal_irq_attach_timestamped(uint8_t slot, uint8_t pin, uint8_t ppi_ch, uint8_t cc,
                                hal_irq_edge_cb_t cb)
{
  if (slot >= HAL_IRQ_MAX_SLOTS || cb == nullptr) {
    return false;
  }
  g_slots[slot].cc = cc;
  g_slots[slot].cb = cb;

  pinMode(pin, INPUT_PULLUP);
  int channel_mask = attachInterrupt(pin, kTrampolines[slot], FALLING);
  if (channel_mask == 0) {
    return false;
  }
  int channel = __builtin_ctz((unsigned)channel_mask);
  return hal_timestamp_route_event(ppi_ch, &NRF_GPIOTE->EVENTS_IN[channel], cc);
}

bool hal_irq_pin_is_low(uint8_t pin)
{
  return digitalRead(pin) == LOW;
}
