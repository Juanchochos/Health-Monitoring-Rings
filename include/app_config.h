#pragma once

// Application tunables. Board wiring lives in board_config.h; this file holds
// sensor settings, DSP parameters, and telemetry options only.

// ---- MAX30003 ECG -----------------------------------------------------------
#define ECG_SAMPLE_RATE_HZ        256     // 128, 256 or 512 (FMSTR = 32768 Hz)
#define ECG_GAIN_VV               40      // 20, 40, 80 or 160; tune from Phase 2 traces
#define ECG_ONCHIP_DHPF_ENABLE    1       // 0.5 Hz digital high-pass in the AFE
#define ECG_ONCHIP_DLPF_HZ        40      // 0 (bypass), 40, 100 or 150
#define ECG_INVERT_POLARITY       0       // swap INP/INN in the input mux
#define ECG_DCLOFF_IMAG_NA        5       // lead-off current: 0 (off), 5, 10, 20, 50, 100
#define ECG_DCLOFF_VTH_MV         500     // lead-off threshold: 300, 400, 450, 500
#define ECG_FIFO_THRESHOLD        16      // INTB asserts at this many unread samples (1-32)

// Pipeline latency of the MAX30003 decimation/DLPF chain, subtracted from every
// ECG timestamp. Fill in from the datasheet "ECG latency" table for the chosen
// rate and DLPF before trusting absolute PAT values.
#define ECG_AFE_LATENCY_US        0

// ---- MAX30102 PPG -----------------------------------------------------------
#define PPG_SAMPLE_RATE_HZ        100     // 50, 100, 200, 400 (411 us pulse width)
#define PPG_PULSE_WIDTH_US        411     // 69, 118, 215, 411
#define PPG_ADC_RANGE_NA          4096    // 2048, 4096, 8192, 16384
#define PPG_LED_RED_PA            0x24    // 0.2 mA/LSB -> 7.2 mA
#define PPG_LED_IR_PA             0x24
#define PPG_SAMPLE_AVG            1       // 1, 2, 4, 8, 16, 32
#define PPG_FIFO_A_FULL           15      // free slots left when INT fires -> 17 samples
#define PPG_FIFO_THRESHOLD        (32 - PPG_FIFO_A_FULL)
#define PPG_AFE_LATENCY_US        0

// ---- Ring buffers (power of two) --------------------------------------------
#define RB_ECG_CAPACITY           1024    // 4 s at 256 Hz
#define RB_PPG_CAPACITY           512     // 5 s at 100 Hz

// ---- Timestamp reconstruction -----------------------------------------------
#define TS_RECON_EMA_ALPHA        0.1f
#define TS_RECON_MAX_DEVIATION    0.05f   // reject edge-derived periods off by > 5 %

// ---- DSP: ECG ---------------------------------------------------------------
#define ECG_SW_BANDPASS_ENABLE    1
#define ECG_HPF_HZ                0.5f
#define ECG_LPF_HZ                40.0f
#define ECG_NOTCH_ENABLE          1
#define ECG_NOTCH_HZ              60.0f   // 50.0f outside the Americas
#define ECG_NOTCH_Q               30.0f
#define ECG_DELAY_REF_HZ          10.0f   // QRS energy band centre for delay compensation

// ---- DSP: PPG ---------------------------------------------------------------
#define PPG_DC_LPF_HZ             0.3f
#define PPG_BP_LO_HZ              0.5f
#define PPG_BP_HI_HZ              5.0f
#define PPG_TIMING_LPF_HZ         8.0f    // foot/peak timing; 5 Hz widens the upstroke (~4 ms bias)
#define PPG_DELAY_REF_HZ          1.5f    // pulse fundamental for delay compensation
#define PPG_FINGER_DC_MIN         50000.0f  // IR DC counts (18-bit) that indicate a finger

// ---- Biometrics ---------------------------------------------------------------
#define PAT_MIN_US                100000UL
#define PAT_MAX_US                600000UL
#define SPO2_COEF_A               (-45.060f)
#define SPO2_COEF_B               30.354f
#define SPO2_COEF_C               94.845f
#define WARMUP_US                 3000000UL
#define WARMUP_BEATS              3

// ---- Telemetry ----------------------------------------------------------------
#define SERIAL_BAUD               115200
#define CSV_PLOTTER_MODE          0       // 1: drop the timestamp column for Serial Plotter
#define STATS_PERIOD_MS           5000
#define VITALS_PERIOD_MS          1000

// The options below can also be overridden per environment with -D flags.
#define TELEM_SERIAL_VITALS       0       // 1 Hz vitals + per-beat lines
#define TELEM_SERIAL_RAW          1       // Phase 2 style timestamp,ecg,red,ir
#ifndef TELEMETRY_SERIAL_MODE
#define TELEMETRY_SERIAL_MODE     TELEM_SERIAL_VITALS
#endif

#ifndef BLE_ENABLE
#define BLE_ENABLE                1
#endif
#define BLE_DEVICE_NAME           "CardioRing"
#ifndef BLE_RAW_STREAM_ENABLE
#define BLE_RAW_STREAM_ENABLE     0       // requests 247-byte MTU + 2M PHY on connect
#endif
