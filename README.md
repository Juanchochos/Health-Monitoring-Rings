# CardioRing firmware

Firmware for a cardiovascular-monitoring ring:
- **MCU:** nRF52840. Prototyped on the Adafruit Feather nRF52840 Express, moving to a Fanstel BT840 PCB.
- **ECG:** MAX30003 over SPI.
- **PPG:** MAX30102 over I2C.

It reports heart rate (ECG), pulse rate and SpO2 (PPG), and pulse arrival time (PAT, from the R-peak to the PPG foot and to the PPG peak). Output goes over USB serial and BLE.

`FIRMWARE_PLAN.md` contains the full design.

## Layout

| Path | Contents |
| --- | --- |
| `include/board_config.h` | Every pin, bus, and peripheral instance (the only place wiring is defined) |
| `include/app_config.h` | Sample rates, gains, filter corners, thresholds, telemetry options |
| `lib/hal`, `lib/drivers` | SPI/I2C/LED/timestamp/IRQ HAL, MAX30003 and MAX30102 drivers |
| `lib/buffering` | Lock-free SPSC ring buffer of `{timestamp_us, raw_sample}` |
| `lib/dsp` | Filters, Pan-Tompkins, PPG processor, SpO2, PAT, rate tracking, pipeline (plain C) |
| `lib/protocol` | BLE payload encoders and shared telemetry types (plain C) |
| `lib/telemetry` | `TelemetrySink` interface, USB serial sink, BLE sink |
| `lib/app` | Phase entry points, acquisition task, DSP task |
| `boards/`, `variants/bt840` | BT840 board definition and identity-map variant |
| `test/` | Unity host tests (`pio test -e native`) |
| `tools/` | Serial logger and live plotter |

## Setup

```bash
uv tool install platformio          # or: pip install platformio
pip install -r tools/requirements.txt
```

### Feather wiring

| Signal | Feather pin | nRF pin |
| --- | --- | --- |
| MAX30003 CSB | D10 | P0.27 |
| MAX30003 INTB | D9 | P0.26 |
| SPI SCK / MOSI / MISO | SCK / MO / MI | P0.14 / P0.13 / P0.15 |
| MAX30102 INT | D5 | P1.08 |
| I2C SDA / SCL | SDA / SCL | P0.12 / P0.11 |
| Status LED | red LED (D3) | P1.15 |

INTB and INT are open-drain, and the firmware enables the internal pull-ups. The MAX30003WING supplies FCLK from its own 32.768 kHz crystal.

## Phases

Each phase is a separate environment. All phases stay buildable for bring-up diagnostics.

```bash
pio run -e <env> -t upload && pio device monitor -b 115200
```

| Env | Phase | What it does |
| --- | --- | --- |
| `feather_phase1` | 1 | Reads MAX30102 PART_ID (must be 0x15) and MAX30003 INFO (non-zero) every 5 s. Prints PASS/FAIL, and runs an I2C scan on failure. |
| `feather_phase2` | 2 | Full register configuration of both ICs, then polled FIFO reads streamed as `timestamp_us,ecg,red,ir` CSV. |
| `feather_phase3` | 3 | GPIOTE edges latched into TIMER3 through PPI, task-driven burst reads into ring buffers, and the same CSV. Prints `#` stats every 5 s (rates, overflows, gaps, latency). |
| `feather_nrf52840` | 4 (default) | Full DSP and telemetry over USB serial and BLE. |
| `bt840` | 4 | Phase 4 on the BT840 PCB. |

Recording and plotting:

```bash
python tools/serial_logger.py /dev/ttyACM0 -o recordings/rest.csv -d 120
python tools/plot_serial.py /dev/ttyACM0 --seconds 5      # raw CSV modes
```

### LED codes

| Blinks | Meaning |
| --- | --- |
| 1 (repeating, Phase 1) | Both sensors pass |
| 2 | MAX30102 identity or configuration failed |
| 3 | MAX30003 identity or configuration failed |
| 4 | Both failed (Phase 1) |
| 5 | Acquisition start failed (GPIOTE/PPI allocation) |
| 6 | DSP task start failed |
| Short flash per heartbeat | Phase 4 running with a valid HR |

### Phase 4 serial output

With `TELEMETRY_SERIAL_MODE = TELEM_SERIAL_VITALS` (the default), the firmware prints:

```
V,t_us,hr_bpm,pr_bpm,spo2_pct,r_ratio,pat_foot_us,pat_peak_us,flags   (1 Hz)
B,r_peak_t_us,rr_us,pat_foot_us,pat_peak_us,flags                    (per R-peak)
# status flags=... ecg=256.0Hz ppg=100.0Hz ecg_fifo_ovf=0 ...         (every 5 s)
```

`TELEM_SERIAL_RAW` brings back the Phase 2/3 raw CSV, with vitals and beats as `#` lines. `serial_logger.py` splits the streams into `<out>.csv`, `<out>_vitals.csv`, `<out>_beats.csv`, and `<out>.log`.

The flags byte is a bit field:

| Bit | Value | Meaning |
| --- | --- | --- |
| 0 | 0x01 | ECG contact |
| 1 | 0x02 | Finger present (PPG) |
| 2 | 0x04 | HR valid |
| 3 | 0x08 | PR valid |
| 4 | 0x10 | SpO2 valid |
| 5 | 0x20 | PAT valid |

A value is valid only after contact plus a 3 s and 3-beat warm-up. Validity clears as soon as contact is lost. Vitals PAT is the median of the last 8 beats, and `B` lines carry per-beat PAT.

### BLE

The device advertises as `CardioRing`. It is visible in nRF Connect and in any app that understands the standard Heart Rate and Pulse Oximeter services.

| Service | Characteristic | Format |
| --- | --- | --- |
| Heart Rate 0x180D | 0x2A37 Measurement (notify) | Flags (contact supported and detected), HR uint8, RR in 1/1024 s |
| | 0x2A38 Body Sensor Location | `3` (finger) |
| Pulse Oximeter 0x1822 | 0x2A5F PLX Continuous (notify, 1 Hz) | flags=0, SpO2 SFLOAT, PR SFLOAT (NaN = 0x07FF when invalid) |
| | 0x2A60 PLX Features | `0x0000` |
| `4f9a0001-7c1e-4b6d-9a2f-3e8c5d1b7a60` | `...0002` Beat/PAT (read, notify) | u32 r_peak_t_us, u32 rr_us, u32 pat_foot_us, u32 pat_peak_us, u8 flags |
| | `...0003` Status (read, notify, 5 s) | u8 flags, u32 ecg_fifo_ovf, u32 ppg_fifo_ovf, u32 rb_ovf, u32 gaps, u16 i2c_err |
| | `...0004` Raw (notify) | u8 n, then n × {u32 t_us, i32 ecg, i32 red, i32 ir} |

All integers are little-endian.

The raw characteristic exists only when built with `-DBLE_RAW_STREAM_ENABLE=1`. In that build, the device requests a 247-byte MTU and the 2M PHY on connect.

### Configuration

Tunables live in `include/app_config.h`. The telemetry switches (`TELEMETRY_SERIAL_MODE`, `BLE_ENABLE`, `BLE_RAW_STREAM_ENABLE`) can also be set per environment, for example:

```ini
[env:feather_raw]
extends = env:feather_nrf52840
build_flags = ${env:feather_nrf52840.build_flags} -DTELEMETRY_SERIAL_MODE=1
```

Two values should be filled in from measurements before trusting absolute numbers:
- `ECG_AFE_LATENCY_US`: from the MAX30003 latency table for the chosen rate and DLPF. Absolute PAT depends on it.
- `SPO2_COEF_*`: currently the Maxim reference curve, which is uncalibrated.

## Host tests

```bash
pio test -e native
```

| Suite | Checks |
| --- | --- |
| `test_ring_buffer` | Empty and full cases, wrap at 2^32, overflow counting, two-thread SPSC stress |
| `test_timestamp_recon` | ±1 % oscillator drift stays within 1 sample over 60 s; outlier rejection; latency removal |
| `test_biquad` | Butterworth responses, more than 30 dB at 60 Hz, priming, analytic group delay against a measured tone burst |
| `test_pan_tompkins` | Synthetic ECG with wander, 60 Hz, noise, inverted polarity, and a 50→150 bpm ramp: ≥ 99 % sensitivity and PPV, R-peak error ≤ 1 sample |
| `test_ppg` | Foot and peak within ±4 ms per beat on low-noise PPG (also with 2 % baseline drift); RMS bounds under noise |
| `test_spo2` | Curve, pairing, median, and ratio of ratios reproduced within 2 % through the full PPG chain |
| `test_pat` | Matcher, outliers, and the rate tracker; full pipeline gives PAT within ±4 ms of a known 250 ms delay; HR, PR, and SpO2 correct; contact and finger loss clear flags |
| `test_ble_payload` | Byte-exact SFLOAT, HRM, PLX, beat, status, and raw encodings |

## BT840 PCB

1. **Pins.** Edit the `BOARD_BT840` block of `include/board_config.h` once the schematic is final. The values there are currently placeholders that mirror the Feather's nRF pins. Values are raw GPIO numbers: use `NRF_PIN(port, pin)`, since the variant maps Arduino pin N to GPIO N. The SPI, I2C, and LED defaults in `variants/bt840/variant.h` derive from `board_config.h`, so nothing else needs editing.
2. **LF clock.** The variant assumes a 32.768 kHz crystal on P0.00/P0.01. If the board has none, build with `-DBT840_LFCLK_RC`.
3. **USB.** Route D+, D−, and VBUS to the connector; the bootloader and the serial console both use USB.
4. **Bootloader (one time, over SWD).** Connect SWDIO, SWCLK, GND, and VDD (target power sense) from a J-Link, or from an nRF52-DK's Debug-out header, to the BT840.
   - Download the Adafruit nRF52 bootloader release for the Feather nRF52840 (`feather_nrf52840_express_bootloader-<ver>_s140_6.1.1.hex`) from [Adafruit_nRF52_Bootloader releases](https://github.com/adafruit/Adafruit_nRF52_Bootloader/releases). It includes SoftDevice S140 6.1.1, which matches `boards/bt840.json`. Because the placeholder pins mirror the Feather, its LED and DFU-button pins line up. If the final PCB moves them, build the bootloader with a custom board under `src/boards/`.
   - Flash it:

     ```bash
     nrfjprog -f nrf52 --recover          # clears APPROTECT on newer nRF52840 silicon (full erase)
     nrfjprog -f nrf52 --program feather_nrf52840_express_bootloader-*_s140_6.1.1.hex --verify --reset
     ```

     With a CMSIS-DAP probe, use `pyocd erase --chip -t nrf52840` followed by `pyocd flash -t nrf52840 <hex>`.
5. **Firmware.** After the bootloader is in place, flashing works over USB exactly like the Feather. Double-tap reset to enter the bootloader if needed.

   ```bash
   pio run -e bt840 -t upload
   ```

## Implementation notes

- **Lead-off.** It is read from the MAX30003 STATUS register on every FIFO service, not routed to INTB. DCLOFFINT would hold INTB low for as long as an electrode is off, which would stall the FIFO interrupt path.
- **Edge timestamps.** They are used only when exactly one new edge arrived for a batch at or above the FIFO threshold. Every other batch is timestamped by polling, counted in `polled=` in the stats.
- **PPG timing signal.** Foot and peak are timed on an 8 Hz low-pass copy of the PPG, which avoids the 0.5 Hz high-pass phase distortion. A 5 Hz corner would widen the upstroke and bias the foot by about 4 ms. The baseline through consecutive pulse troughs is removed before the peak search, so respiratory or drift tilt doesn't move the peak.
- **PPG latency.** A PPG beat is final once the next pulse's upstroke starts, so beat events (and their PAT) arrive about one beat late. Without a following pulse, a 2 s timeout finalizes it.
