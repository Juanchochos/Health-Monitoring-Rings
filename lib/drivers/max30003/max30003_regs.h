#pragma once

// MAX30003 register map and bitfields (24-bit registers).

#define MAX30003_REG_NO_OP          0x00
#define MAX30003_REG_STATUS         0x01
#define MAX30003_REG_EN_INT         0x02
#define MAX30003_REG_EN_INT2        0x03
#define MAX30003_REG_MNGR_INT       0x04
#define MAX30003_REG_MNGR_DYN       0x05
#define MAX30003_REG_SW_RST         0x08
#define MAX30003_REG_SYNCH          0x09
#define MAX30003_REG_FIFO_RST       0x0A
#define MAX30003_REG_INFO           0x0F
#define MAX30003_REG_CNFG_GEN       0x10
#define MAX30003_REG_CNFG_CAL       0x12
#define MAX30003_REG_CNFG_EMUX      0x14
#define MAX30003_REG_CNFG_ECG       0x15
#define MAX30003_REG_CNFG_RTOR1     0x1D
#define MAX30003_REG_CNFG_RTOR2     0x1E
#define MAX30003_REG_ECG_FIFO_BURST 0x20
#define MAX30003_REG_ECG_FIFO       0x21
#define MAX30003_REG_RTOR           0x25

#define MAX30003_CMD_READ(reg)      ((uint8_t)(((reg) << 1) | 0x01))
#define MAX30003_CMD_WRITE(reg)     ((uint8_t)((reg) << 1))

// STATUS (0x01)
#define MAX30003_STATUS_EINT        (1UL << 23)
#define MAX30003_STATUS_EOVF        (1UL << 22)
#define MAX30003_STATUS_FSTINT      (1UL << 21)
#define MAX30003_STATUS_DCLOFFINT   (1UL << 20)
#define MAX30003_STATUS_LONINT      (1UL << 11)
#define MAX30003_STATUS_RRINT       (1UL << 10)
#define MAX30003_STATUS_SAMP        (1UL << 9)
#define MAX30003_STATUS_PLLINT      (1UL << 8)
#define MAX30003_STATUS_LDOFF_PH    (1UL << 3)
#define MAX30003_STATUS_LDOFF_PL    (1UL << 2)
#define MAX30003_STATUS_LDOFF_NH    (1UL << 1)
#define MAX30003_STATUS_LDOFF_NL    (1UL << 0)
#define MAX30003_STATUS_LDOFF_MASK  0x0FUL

// EN_INT / EN_INT2 (0x02 / 0x03)
#define MAX30003_EN_EINT            (1UL << 23)
#define MAX30003_EN_EOVF            (1UL << 22)
#define MAX30003_EN_FSTINT          (1UL << 21)
#define MAX30003_EN_DCLOFFINT       (1UL << 20)
#define MAX30003_EN_LONINT          (1UL << 11)
#define MAX30003_EN_RRINT           (1UL << 10)
#define MAX30003_EN_SAMP            (1UL << 9)
#define MAX30003_EN_PLLINT          (1UL << 8)
#define MAX30003_INTB_TYPE(x)       ((uint32_t)(x) & 0x3UL)
#define MAX30003_INTB_DISABLED      0
#define MAX30003_INTB_CMOS          1
#define MAX30003_INTB_OPEN_DRAIN    2
#define MAX30003_INTB_OPEN_DRAIN_PU 3   // open-drain with internal 125 kOhm pull-up

// MNGR_INT (0x04)
#define MAX30003_MNGR_EFIT(n)       (((uint32_t)(n) & 0x1FUL) << 19)  // interrupt at n+1 samples
#define MAX30003_MNGR_CLR_FAST      (1UL << 6)
#define MAX30003_MNGR_CLR_RRINT(x)  (((uint32_t)(x) & 0x3UL) << 4)
#define MAX30003_MNGR_CLR_SAMP      (1UL << 2)
#define MAX30003_MNGR_SAMP_IT(x)    ((uint32_t)(x) & 0x3UL)

// CNFG_GEN (0x10)
#define MAX30003_GEN_EN_ULP_LON(x)  (((uint32_t)(x) & 0x3UL) << 22)
#define MAX30003_GEN_FMSTR(x)       (((uint32_t)(x) & 0x3UL) << 20)
#define MAX30003_GEN_EN_ECG         (1UL << 19)
#define MAX30003_GEN_EN_DCLOFF(x)   (((uint32_t)(x) & 0x3UL) << 12)
#define MAX30003_GEN_DCLOFF_IPOL    (1UL << 11)
#define MAX30003_GEN_DCLOFF_IMAG(x) (((uint32_t)(x) & 0x7UL) << 8)
#define MAX30003_GEN_DCLOFF_VTH(x)  (((uint32_t)(x) & 0x3UL) << 6)
#define MAX30003_GEN_EN_RBIAS(x)    (((uint32_t)(x) & 0x3UL) << 4)
#define MAX30003_GEN_RBIASV(x)      (((uint32_t)(x) & 0x3UL) << 2)
#define MAX30003_GEN_RBIASP         (1UL << 1)
#define MAX30003_GEN_RBIASN         (1UL << 0)

#define MAX30003_FMSTR_32768        0   // ECG rates 512 / 256 / 128 sps
#define MAX30003_RBIAS_ECG          1
#define MAX30003_RBIASV_100M        1
#define MAX30003_DCLOFF_ECG         1

// CNFG_EMUX (0x14)
#define MAX30003_EMUX_POL           (1UL << 23)
#define MAX30003_EMUX_OPENP         (1UL << 21)
#define MAX30003_EMUX_OPENN         (1UL << 20)
#define MAX30003_EMUX_CALP_SEL(x)   (((uint32_t)(x) & 0x3UL) << 18)
#define MAX30003_EMUX_CALN_SEL(x)   (((uint32_t)(x) & 0x3UL) << 16)

// CNFG_ECG (0x15)
#define MAX30003_ECG_RATE(x)        (((uint32_t)(x) & 0x3UL) << 22)
#define MAX30003_ECG_GAIN(x)        (((uint32_t)(x) & 0x3UL) << 16)
#define MAX30003_ECG_DHPF           (1UL << 14)
#define MAX30003_ECG_DLPF(x)        (((uint32_t)(x) & 0x3UL) << 12)
#define MAX30003_ECG_FIELDS_MASK    (MAX30003_ECG_RATE(3) | MAX30003_ECG_GAIN(3) | \
                                     MAX30003_ECG_DHPF | MAX30003_ECG_DLPF(3))

// ECG FIFO word: sample[23:6] (18-bit two's complement), ETAG[5:3], PTAG[2:0]
#define MAX30003_ETAG(word)         (((word) >> 3) & 0x7UL)
#define MAX30003_ETAG_VALID         0
#define MAX30003_ETAG_FAST          1
#define MAX30003_ETAG_VALID_EOF     2
#define MAX30003_ETAG_FAST_EOF      3
#define MAX30003_ETAG_EMPTY         6
#define MAX30003_ETAG_OVERFLOW      7

#define MAX30003_FIFO_DEPTH         32
