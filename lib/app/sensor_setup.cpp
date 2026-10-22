#include "sensor_setup.h"

#include <Arduino.h>

#include "app_config.h"
#include "hal_led.h"
#include "max30003/max30003.h"
#include "max30102/max30102.h"

bool sensors_configure_ecg()
{
  max30003_config_t cfg = {};
  cfg.rate_sps = ECG_SAMPLE_RATE_HZ;
  cfg.gain_vv = ECG_GAIN_VV;
  cfg.dhpf_enable = ECG_ONCHIP_DHPF_ENABLE;
  cfg.dlpf_hz = ECG_ONCHIP_DLPF_HZ;
  cfg.invert_polarity = ECG_INVERT_POLARITY;
  cfg.dcloff_imag_na = ECG_DCLOFF_IMAG_NA;
  cfg.dcloff_vth_mv = ECG_DCLOFF_VTH_MV;
  cfg.fifo_threshold = ECG_FIFO_THRESHOLD;
  return max30003_configure(&cfg);
}

bool sensors_configure_ppg()
{
  max30102_config_t cfg = {};
  cfg.sample_rate_hz = PPG_SAMPLE_RATE_HZ;
  cfg.pulse_width_us = PPG_PULSE_WIDTH_US;
  cfg.adc_range_na = PPG_ADC_RANGE_NA;
  cfg.led_red_pa = PPG_LED_RED_PA;
  cfg.led_ir_pa = PPG_LED_IR_PA;
  cfg.sample_avg = PPG_SAMPLE_AVG;
  cfg.fifo_a_full = PPG_FIFO_A_FULL;
  return max30102_configure(&cfg);
}

void sensors_bring_up_or_halt()
{
  max30003_init_bus();
  max30102_init_bus();

  if (!sensors_configure_ppg()) {
    Serial.println("# FATAL: MAX30102 configuration failed (run Phase 1 to check the bus)");
    hal_led_halt_with_code(kBlinkPpgConfigFail);
  }
  if (!sensors_configure_ecg()) {
    Serial.println("# FATAL: MAX30003 configuration failed (CNFG_ECG read-back mismatch)");
    hal_led_halt_with_code(kBlinkEcgConfigFail);
  }
}
