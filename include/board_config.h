#pragma once

// Single source of truth for every GPIO, bus, and peripheral-instance
// assignment. Pin values are Arduino pin numbers as understood by the active
// variant: on the Feather they index the Adafruit g_ADigitalPinMap; the BT840
// variant uses an identity map, so its values are raw nRF GPIO numbers
// (port * 32 + pin).

#define NRF_PIN(port, pin) (((port) << 5) | ((pin) & 0x1F))

#if defined(BOARD_FEATHER_NRF52840)

  #define BOARD_NAME            "Adafruit Feather nRF52840 Express"

  #define PIN_ECG_CS            10        // P0.27 | Chip Select for the MAX30003
  #define PIN_ECG_INTB          9         // P0.26 | Interrupt for the MAX30003
  #define PIN_PPG_INT           5         // P1.08 | Interrupt for the MAX30102

  #define PIN_ECG_SCK           PIN_SPI_SCK    // P0.14 | Serial Clock for the MAX30003
  #define PIN_ECG_MOSI          PIN_SPI_MOSI   // P0.13 | Serial Master Out Slave In for the MAX30003
  #define PIN_ECG_MISO          PIN_SPI_MISO   // P0.15 | Serial Master In Slave Out for the MAX30003

  #define PIN_PPG_SDA           PIN_WIRE_SDA   // P0.12 | Serial Data for the MAX30102
  #define PIN_PPG_SCL           PIN_WIRE_SCL   // P0.11 | Serial Clock for the MAX30102

  #define PIN_STATUS_LED        LED_RED   // P1.15 | Status LED for the Feather
  #define STATUS_LED_ON_LEVEL   HIGH  // HIGH means the LED is on when the pin is HIGH

#elif defined(BOARD_BT840)  //Worry about these when we build the BT840 PCB

  #define BOARD_NAME            "Fanstel BT840 custom PCB"

  // TODO(pcb): placeholders mirror the Feather's physical nRF pins until the
  // BT840 schematic is final.
  #define PIN_ECG_CS            NRF_PIN(0, 27)
  #define PIN_ECG_INTB          NRF_PIN(0, 26)
  #define PIN_PPG_INT           NRF_PIN(1, 8)

  #define PIN_ECG_SCK           NRF_PIN(0, 14)
  #define PIN_ECG_MOSI          NRF_PIN(0, 13)
  #define PIN_ECG_MISO          NRF_PIN(0, 15)

  #define PIN_PPG_SDA           NRF_PIN(0, 12)
  #define PIN_PPG_SCL           NRF_PIN(0, 11)

  #define PIN_STATUS_LED        NRF_PIN(1, 15)
  #define STATUS_LED_ON_LEVEL   HIGH

#else
  #error "Define BOARD_FEATHER_NRF52840 or BOARD_BT840 (see platformio.ini)"
#endif

// ---- Buses ---------------------------------------------------------------
#define ECG_SPI_PORT            SPI
#define ECG_SPI_HZ              4000000UL   // MAX30003: SPI mode 0, max 12 MHz

#define PPG_I2C_PORT            Wire
#define PPG_I2C_HZ              400000UL
#define PPG_I2C_ADDR            0x57  // Default address for the MAX30102

// Wire RX buffer is 64 bytes; chunks must also be a multiple of the 6-byte
// MAX30102 Red+IR sample so no transaction ends mid-sample.
#define I2C_MAX_CHUNK_BYTES     30

// ---- Hardware timestamping -------------------------------------------------
// TIMER0 belongs to the SoftDevice and the core touches TIMER2.
#define TS_TIMER                NRF_TIMER3
#define TS_CC_ECG               0
#define TS_CC_PPG               1
#define TS_CC_NOW               2

// PPI channels 0-16 are application-owned; S140 reserves 17-31.
#define TS_PPI_CH_ECG           0
#define TS_PPI_CH_PPG           1
