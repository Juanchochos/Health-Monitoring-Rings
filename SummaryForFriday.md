# Understanding the code and what to test on Friday

Short answer: yes, there is code for the basic sensor tests. It's Phase 1 and Phase 2, and both are meant for your first assembly. Nothing here has run on hardware yet.

## Structure in one picture

```
Sensors → HAL/Drivers → Ring buffers → DSP → Telemetry (USB serial / BLE)
```

| Layer | Folder | Job |
|---|---|---|
| Wiring | `include/board_config.h` | The only place pins and buses are defined |
| Settings | `include/app_config.h` | Sample rates, gains, filter corners, thresholds |
| HAL | `lib/hal` | Raw SPI, I2C, LED, timer and interrupt access |
| Drivers | `lib/drivers/max30003` (ECG), `max30102` (PPG) | Register maps, configuration, FIFO reads |
| Buffering | `lib/buffering` | Lock-free queue of `{timestamp, sample}` |
| DSP | `lib/dsp` | Filters, QRS detection, SpO2, PAT |
| Telemetry | `lib/telemetry`, `lib/protocol` | Serial and BLE output |
| Phases | `lib/app/phase1..4.cpp` | One program per milestone |

You pick a phase by choosing a PlatformIO environment. Each one builds a separate program, and Phases 1-3 stay available as bring-up tools.

## What to test first (in order)

**Test 1: Are the chips alive? (Phase 1, `feather_phase1`)**
- It reads the MAX30102 `PART_ID` over I2C, which must be `0x15`.
- It reads the MAX30003 `INFO` register over SPI, which must be non-zero (and not `0xFFFFFF`).
- It repeats every 5 s, so you can reseat wires while watching the monitor.
- It prints `[PASS]` or `[FAIL]` with a diagnosis:
  - No I2C ACK triggers an I2C scan.
  - `INFO = 0` means MISO is stuck low or the chip is unpowered.
  - `INFO = 0xFFFFFF` means MISO is stuck high, so check CS, SCK and MISO.
- The LED blinks 1 for pass, 2 for PPG fail, 3 for ECG fail, and 4 for both.

What this proves: power, ground, SPI and I2C wiring, and chip addresses are all correct. If this fails, nothing else matters yet.

**Test 2: Do the chips produce data? (Phase 2, `feather_phase2`)**
- It soft-resets both chips and writes their full register configuration.
- It reads back registers to verify the write.
- It polls the FIFOs every 10 ms and prints `timestamp_us,ecg,red,ir` over USB.
- Plot it with `tools/plot_serial.py`, or record it with `tools/serial_logger.py`.
- It prints rate statistics, so you can check you really get about 256 Hz ECG and 100 Hz PPG.

What to look for:
- **PPG:** finger on the sensor gives a large DC value (tens of thousands of counts) with a small pulsing on top. Finger off gives a low value.
- **ECG:** holding both electrodes should show QRS spikes. Saturated or flat traces point to electrode contact or gain problems.
- Tune `ECG_GAIN_VV` and the LED currents from these traces.

**Test 3 (if time allows): Is timing trustworthy? (Phase 3, `feather_phase3`)**
- Interrupt-driven acquisition with hardware timestamps.
- The `#` stats lines report overflows, gaps, and polled-versus-edge reads.
- The goal is zero overflows or gaps over a few minutes. It matters before trusting PAT.

Phase 4 (`feather_nrf52840`) is the full HR, SpO2, PAT and BLE system. Save it for after Tests 1-3 pass.

## What you need

**Hardware**
- Adafruit Feather nRF52840 Express, plus a USB cable.
- MAX30003WING board, with its onboard 32.768 kHz crystal as the clock.
- MAX30102 breakout.
- Two electrodes, or leads you can hold, for ECG.
- Jumper wires and a finger.

**Wiring (Feather)**

| Signal | Feather pin |
|---|---|
| MAX30003 CS | D10 |
| MAX30003 INTB | D9 |
| SPI SCK / MOSI / MISO | SCK / MO / MI |
| MAX30102 INT | D5 |
| I2C SDA / SCL | SDA / SCL |
| Power | 3.3 V and GND to both boards |

Check that your boards' logic and supply voltages match the Feather's 3.3 V, and that the I2C lines have pull-ups (most breakouts include them).

**Software**
- PlatformIO. `uv tool install platformio` is how it was installed here.
- `pip install -r tools/requirements.txt` for the logger and plotter.

**Commands**
```bash
pio run -e feather_phase1 -t upload
pio device monitor -b 115200
```
Swap in `feather_phase2` for the next test.

## Caveats to tell your team
- The firmware builds and passes simulated tests, but has never run on real chips. Expect small fixes during first bring-up, especially register settings.
- Absolute PAT needs `ECG_AFE_LATENCY_US` filled in from the MAX30003 datasheet. It's 0 for now.
- SpO2 uses an uncalibrated generic curve, so only judge it as plausible, around 95-100% at rest.
- Pins for the final BT840 PCB are placeholders, so Friday is Feather-only.

## Suggested Friday agenda
1. Power up and run Test 1. Goal: both chips PASS.
2. Run Test 2. Goal: a visible PPG pulse and ECG QRS complexes.
3. Record 1-2 minutes with `serial_logger.py` as a baseline.
4. Note problems (noise, saturation, rate mismatches) for the next round.


---


# Study plan: Phase 1 and Phase 2

Phase 1 and 2 use only a small part of the code. This is a reading order and what to take from each file.

## Phase 1: "Are the chips alive?" (about 1 hour)

Read these in order:

1. **`include/board_config.h`** (5 min)
   - Know which Feather pin goes to which signal (CS D10, INTB D9, INT D5, SPI and I2C pins).
   - You should be able to say why wiring lives in one file.
2. **`lib/hal/hal_i2c.cpp` and `hal_spi.cpp`** (15 min)
   - These are thin wrappers over Arduino's `Wire` and `SPI`.
   - Know the key difference: I2C addresses a device (0x57), while SPI uses a chip-select pin to pick the device.
3. **`lib/drivers/max30102/max30102.cpp`**, function `read_part_id` (10 min)
   - It reads register `0xFF`. The answer must be `0x15`.
4. **`lib/drivers/max30003/max30003.cpp`**, function `read_info` (10 min)
   - It reads register `0x0F`. The answer must be non-zero.
   - Skim the SPI command format. A read is a command byte (address plus read bit), then 3 data bytes.
5. **`lib/app/phase1.cpp`** (15 min)
   - This is the whole test. It's about 120 lines and easy to read top to bottom.
   - Note how failures are diagnosed: stuck-low versus stuck-high MISO, and the I2C scan.

**You should be able to answer:**
- What does a PASS prove, and what doesn't it prove? It proves power, wiring and addressing. It says nothing about signal quality.
- If the MAX30003 returns `0x000000`, what do you check first? Power or MISO.
- If it returns `0xFFFFFF`? CS, SCK or MISO wiring.

## Phase 2: "Do the chips produce data?" (about 2 hours)

1. **`include/app_config.h`**, ECG and PPG sections (15 min)
   - Know the main knobs: ECG 256 Hz and gain 40 V/V, PPG 100 Hz and LED current `0x24`.
   - These are what you'll tune on Friday.
2. **`lib/app/sensor_setup.cpp`** (10 min)
   - It turns the settings above into driver configurations. Both phases share it.
3. **`max30102.cpp`, function `configure`** (20 min)
   - Reset, set the FIFO, set SpO2 mode (Red and IR), set LED currents, then read back to verify.
   - Concept to grasp: the FIFO holds up to 32 samples, and you read them in bursts.
4. **`max30003.cpp`, functions `configure` and `read_fifo`** (30 min)
   - Reset, then set the general, input-mux and ECG-rate registers.
   - Lead-off and the on-chip bias are set up here too.
   - Each FIFO word is 24 bits: an 18-bit signed sample plus a 3-bit tag. The tag says valid, empty or overflow. Skim how it is sign-extended.
5. **`lib/app/phase2.cpp`** (20 min)
   - It polls every 10 ms and prints `timestamp_us,ecg,red,ir`.
   - Note that the two sensors run at different rates (256 and 100 Hz), so the PPG value is held between samples.
6. **`tools/serial_logger.py` and `plot_serial.py`** (10 min)
   - Know how to record and plot.

**You should be able to answer:**
- Why read the FIFO in a burst instead of one sample at a time?
- What would a valid PPG signal look like in the CSV? A large DC value with a small pulsing.
- What do the `#` lines tell you? The measured sample rates and error counts.

## What to skip for now
Ring buffers, interrupts and hardware timestamps (Phase 3), plus the DSP and BLE code (Phase 4). The host tests can also wait. They simulate signals and don't touch the sensors.

## Before Friday
- Install the tools and build once: `pio run -e feather_phase1`. Do the same for `feather_phase2`. This confirms your environment works before you have hardware in hand.
- The datasheets are worth a look:
  - MAX30102: register map and FIFO sections.
  - MAX30003: SPI interface and the ECG FIFO section.
- Print or save the wiring table above.
