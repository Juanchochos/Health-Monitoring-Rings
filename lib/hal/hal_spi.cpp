#include "hal_spi.h"

#include <Arduino.h>
#include <SPI.h>

#include "board_config.h"

static const SPISettings kEcgSpiSettings(ECG_SPI_HZ, MSBFIRST, SPI_MODE0);

void hal_spi_init(void)
{
  pinMode(PIN_ECG_CS, OUTPUT);
  digitalWrite(PIN_ECG_CS, HIGH);

  ECG_SPI_PORT.setPins(PIN_ECG_MISO, PIN_ECG_SCK, PIN_ECG_MOSI);
  ECG_SPI_PORT.begin();
}

void hal_spi_select(void)
{
  ECG_SPI_PORT.beginTransaction(kEcgSpiSettings);
  digitalWrite(PIN_ECG_CS, LOW);
}

void hal_spi_deselect(void)
{
  digitalWrite(PIN_ECG_CS, HIGH);
  ECG_SPI_PORT.endTransaction();
}

void hal_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
  ECG_SPI_PORT.transfer(tx, rx, len);
}
