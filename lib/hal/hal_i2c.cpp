#include "hal_i2c.h"

#include <Arduino.h>
#include <Wire.h>

#include "board_config.h"

static bool read_chunk(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len)
{
  PPG_I2C_PORT.beginTransmission(addr); // Start the I2C transmission to the address
  PPG_I2C_PORT.write(reg); // Write the register to the address
  if (PPG_I2C_PORT.endTransmission(false) != 0) { // Keep the bus open for reading
    return false;
  }
  if (PPG_I2C_PORT.requestFrom(addr, (uint8_t)len) != len) { // Send the request to read
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    buf[i] = (uint8_t)PPG_I2C_PORT.read(); // Read the data from the address
  }
  return true;
}

void hal_i2c_init(void)
{
  PPG_I2C_PORT.setPins(PIN_PPG_SDA, PIN_PPG_SCL);
  PPG_I2C_PORT.begin();
  PPG_I2C_PORT.setClock(PPG_I2C_HZ);
}

bool hal_i2c_probe(uint8_t addr)
{
  PPG_I2C_PORT.beginTransmission(addr);
  return PPG_I2C_PORT.endTransmission() == 0;
}

bool hal_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
  PPG_I2C_PORT.beginTransmission(addr);
  PPG_I2C_PORT.write(reg);
  PPG_I2C_PORT.write(value);
  return PPG_I2C_PORT.endTransmission() == 0;
}

bool hal_i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len)
{
  size_t done = 0;
  while (done < len) {
    size_t n = len - done;
    if (n > I2C_MAX_CHUNK_BYTES) {
      n = I2C_MAX_CHUNK_BYTES;
    }
    if (!read_chunk(addr, (uint8_t)(reg + done), buf + done, n)) {
      return false;
    }
    done += n;
  }
  return true;
}

bool hal_i2c_read_stream(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len)
{
  size_t done = 0;
  while (done < len) {
    size_t n = len - done;
    if (n > I2C_MAX_CHUNK_BYTES) {
      n = I2C_MAX_CHUNK_BYTES;
    }
    if (!read_chunk(addr, reg, buf + done, n)) {
      return false;
    }
    done += n;
  }
  return true;
}
