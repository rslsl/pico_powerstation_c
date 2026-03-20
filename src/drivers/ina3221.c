// ============================================================
// drivers/ina3221.c
// ============================================================
#include "ina3221.h"
#include "config.h"
#include <math.h>
#include <stdio.h>

static bool _rdreg(INA3221 *d, uint8_t reg, uint16_t *out) {
    uint8_t buf[2];
    if (i2c_write_timeout_us(d->i2c, d->addr, &reg, 1, true,  I2C_TIMEOUT_US) != 1) return false;
    if (i2c_read_timeout_us (d->i2c, d->addr, buf,  2, false, I2C_TIMEOUT_US) != 2) return false;
    *out = (uint16_t)((buf[0] << 8) | buf[1]);
    return true;
}

bool ina3221_init(INA3221 *dev, i2c_inst_t *i2c, TCA9548A *tca,
                  uint8_t tca_ch, uint8_t addr) {
    dev->i2c    = i2c;
    dev->tca    = tca;
    dev->tca_ch = tca_ch;
    dev->addr   = addr;
    if (!tca_select(tca, tca_ch)) return false;
    // Config: all 3 channels ON, 8 averages, 1.1ms conv
    uint8_t cfg[3] = {INA3221_REG_CFG, 0x72, 0x27};
    return i2c_write_timeout_us(i2c, addr, cfg, 3, false, I2C_TIMEOUT_US) == 3;
}

bool ina3221_read_cells(INA3221 *dev,
                        float *v_b1, float *v_b2, float *v_b3,
                        float *delta_mv) {
    if (!tca_select(dev->tca, dev->tca_ch)) return false;

    uint16_t r1, r2, r3;
    if (!_rdreg(dev, INA3221_REG_BUS1, &r1)) return false;
    if (!_rdreg(dev, INA3221_REG_BUS2, &r2)) return false;
    if (!_rdreg(dev, INA3221_REG_BUS3, &r3)) return false;

    // The board exposes cumulative 3S taps relative to pack negative:
    // ch1 = B1, ch2 = B1+B2, ch3 = B1+B2+B3.
    // INA3221 bus voltage LSB = 8mV (bus reg >> 3).
    float tap1 = (float)(r1 >> 3) * 0.008f;
    float tap2 = (float)(r2 >> 3) * 0.008f;
    float tap3 = (float)(r3 >> 3) * 0.008f;

    float b1 = tap1;
    float b2 = tap2 - tap1;
    float b3 = tap3 - tap2;

    // Validate both the cumulative taps and the derived cell voltages.
    if (tap1 < 2.5f || tap1 > 4.5f) return false;
    if (tap2 < 5.0f || tap2 > 9.0f) return false;
    if (tap3 < 8.0f || tap3 > 14.0f) return false;
    if (b1 < 2.5f || b1 > 4.5f) return false;
    if (b2 < 2.5f || b2 > 4.5f) return false;
    if (b3 < 2.5f || b3 > 4.5f) return false;

    float mn = fminf(b1, fminf(b2, b3));
    float mx = fmaxf(b1, fmaxf(b2, b3));

    *v_b1      = b1;
    *v_b2      = b2;
    *v_b3      = b3;
    *delta_mv  = (mx - mn) * 1000.0f;
    return true;
}
