#pragma once

// MAX30102 register map and bitfields.

#define MAX30102_REG_INT_STATUS_1   0x00
#define MAX30102_REG_INT_STATUS_2   0x01
#define MAX30102_REG_INT_ENABLE_1   0x02
#define MAX30102_REG_INT_ENABLE_2   0x03
#define MAX30102_REG_FIFO_WR_PTR    0x04
#define MAX30102_REG_OVF_COUNTER    0x05
#define MAX30102_REG_FIFO_RD_PTR    0x06
#define MAX30102_REG_FIFO_DATA      0x07
#define MAX30102_REG_FIFO_CONFIG    0x08
#define MAX30102_REG_MODE_CONFIG    0x09
#define MAX30102_REG_SPO2_CONFIG    0x0A
#define MAX30102_REG_LED1_PA        0x0C   // Red
#define MAX30102_REG_LED2_PA        0x0D   // IR
#define MAX30102_REG_MULTI_LED_1    0x11
#define MAX30102_REG_MULTI_LED_2    0x12
#define MAX30102_REG_TEMP_INT       0x1F
#define MAX30102_REG_TEMP_FRAC      0x20
#define MAX30102_REG_TEMP_CONFIG    0x21
#define MAX30102_REG_REV_ID         0xFE
#define MAX30102_REG_PART_ID        0xFF

#define MAX30102_PART_ID_VALUE      0x15

// INT_STATUS_1 / INT_ENABLE_1
#define MAX30102_INT_A_FULL         (1u << 7)
#define MAX30102_INT_PPG_RDY        (1u << 6)
#define MAX30102_INT_ALC_OVF        (1u << 5)
#define MAX30102_INT_PWR_RDY        (1u << 0)

// FIFO_CONFIG
#define MAX30102_FIFO_SMP_AVE(x)    (((x) & 0x7u) << 5)
#define MAX30102_FIFO_ROLLOVER_EN   (1u << 4)
#define MAX30102_FIFO_A_FULL(x)     ((x) & 0xFu)

// MODE_CONFIG
#define MAX30102_MODE_SHDN          (1u << 7)
#define MAX30102_MODE_RESET         (1u << 6)
#define MAX30102_MODE_HR            0x02
#define MAX30102_MODE_SPO2          0x03
#define MAX30102_MODE_MULTI_LED     0x07

// SPO2_CONFIG
#define MAX30102_SPO2_ADC_RGE(x)    (((x) & 0x3u) << 5)
#define MAX30102_SPO2_SR(x)         (((x) & 0x7u) << 2)
#define MAX30102_SPO2_LED_PW(x)     ((x) & 0x3u)

#define MAX30102_FIFO_DEPTH         32
#define MAX30102_BYTES_PER_SAMPLE   6      // Red[3] then IR[3] in SpO2 mode
#define MAX30102_SAMPLE_MASK        0x3FFFFu
