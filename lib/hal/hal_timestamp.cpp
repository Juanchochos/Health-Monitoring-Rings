#include "hal_timestamp.h"

#include <Arduino.h>
#include <nrf_sdm.h>
#include <nrf_soc.h>

#include "board_config.h"

void hal_timestamp_init(void)
{
  TS_TIMER->TASKS_STOP = 1;
  TS_TIMER->TASKS_CLEAR = 1;
  TS_TIMER->MODE = TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos;
  TS_TIMER->BITMODE = TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos;
  TS_TIMER->PRESCALER = 4;  // 16 MHz / 2^4 = 1 MHz
  TS_TIMER->INTENCLR = 0xFFFFFFFF;
  TS_TIMER->SHORTS = 0;
  TS_TIMER->TASKS_START = 1;
}

uint32_t hal_timestamp_now_us(void)
{
  taskENTER_CRITICAL();
  TS_TIMER->TASKS_CAPTURE[TS_CC_NOW] = 1;
  uint32_t now = TS_TIMER->CC[TS_CC_NOW];
  taskEXIT_CRITICAL();
  return now;
}

uint32_t hal_timestamp_captured_us(uint8_t cc)
{
  return TS_TIMER->CC[cc];
}

bool hal_timestamp_route_event(uint8_t ppi_ch, volatile uint32_t *event, uint8_t cc)
{
  volatile uint32_t *task = &TS_TIMER->TASKS_CAPTURE[cc];

  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    return sd_ppi_channel_assign(ppi_ch, event, task) == NRF_SUCCESS &&
           sd_ppi_channel_enable_set(1UL << ppi_ch) == NRF_SUCCESS;
  }

  NRF_PPI->CH[ppi_ch].EEP = (uint32_t)event;
  NRF_PPI->CH[ppi_ch].TEP = (uint32_t)task;
  NRF_PPI->CHENSET = 1UL << ppi_ch;
  return true;
}
