// Fanstel BT840 (nRF52840) variant for the Adafruit nRF52 Arduino core.
//
// Arduino pin N is nRF GPIO N (port * 32 + pin): g_ADigitalPinMap is an
// identity map. Bus and LED defaults come from include/board_config.h so the
// PCB wiring is still defined in exactly one place.

#ifndef _VARIANT_BT840_
#define _VARIANT_BT840_

#define VARIANT_MCK       (64000000ul)

// LF clock for the SoftDevice. Assumes a 32.768 kHz crystal on P0.00/P0.01;
// build with -DBT840_LFCLK_RC if neither the module nor the PCB provides one.
// TODO(pcb): confirm against the BT840 datasheet and the final schematic.
#ifdef BT840_LFCLK_RC
#define USE_LFRC
#else
#define USE_LFXO
#endif

#include "WVariant.h"
#include "../../include/board_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PINS_COUNT           (48)
#define NUM_DIGITAL_PINS     (48)
#define NUM_ANALOG_INPUTS    (8)
#define NUM_ANALOG_OUTPUTS   (0)

// LEDs (Bluefruit's auto connection LED uses LED_BLUE; the firmware disables it)
#define PIN_LED1             PIN_STATUS_LED
#define PIN_LED2             PIN_STATUS_LED
#define LED_BUILTIN          PIN_LED1
#define LED_CONN             PIN_LED2
#define LED_RED              PIN_LED1
#define LED_BLUE             PIN_LED2
#define LED_STATE_ON         STATUS_LED_ON_LEVEL

// SAADC inputs AIN0..AIN7 are fixed by the silicon.
#define PIN_A0               NRF_PIN(0, 2)
#define PIN_A1               NRF_PIN(0, 3)
#define PIN_A2               NRF_PIN(0, 4)
#define PIN_A3               NRF_PIN(0, 5)
#define PIN_A4               NRF_PIN(0, 28)
#define PIN_A5               NRF_PIN(0, 29)
#define PIN_A6               NRF_PIN(0, 30)
#define PIN_A7               NRF_PIN(0, 31)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION       14

#define PIN_AREF             PIN_A7
static const uint8_t AREF = PIN_AREF;

// UART (unused by the firmware; USB CDC is the console)
#define PIN_SERIAL1_RX       NRF_PIN(0, 8)
#define PIN_SERIAL1_TX       NRF_PIN(0, 6)

// SPI: the MAX30003 bus
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO         PIN_ECG_MISO
#define PIN_SPI_MOSI         PIN_ECG_MOSI
#define PIN_SPI_SCK          PIN_ECG_SCK

static const uint8_t SS   = PIN_ECG_CS;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;

// I2C: the MAX30102 bus
#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA         PIN_PPG_SDA
#define PIN_WIRE_SCL         PIN_PPG_SCL

#ifdef __cplusplus
}
#endif

#endif
