// ============================================================
// drivers/lm75a.c - LM75A I2C Temperature Sensor
// ============================================================
#include "lm75a.h"
#include "config.h"
#include <stdio.h>

#define LM75A_REG_TEMP   0x00
#define LM75A_REG_CONF   0x01
#define LM75A_REG_THYST  0x02
#define LM75A_REG_TOS    0x03

static bool _write16(LM75A *d, uint8_t reg, int16_t val) {
    uint8_t buf[3] = {reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    return i2c_write_timeout_us(d->i2c, d->addr, buf, 3,
                                false, I2C_TIMEOUT_US) == 3;
}

static int16_t _celsius_to_reg(float t) {
    // LM75A stores a 9-bit temperature value in the upper bits.
    return (int16_t)((int)(t * 2.0f) << 7);
}

bool lm75a_init(LM75A *dev, i2c_inst_t *i2c, TCA9548A *tca,
                uint8_t tca_ch, uint8_t addr,
                float t_os_c, float t_hyst_c) {
    dev->i2c    = i2c;
    dev->tca    = tca;
    dev->tca_ch = tca_ch;
    dev->addr   = addr;

    if (!tca_select(tca, tca_ch)) return false;

    // Normal mode, OS active low, comparator, 1 fault.
    uint8_t cfg[2] = {LM75A_REG_CONF, 0x00};
    if (i2c_write_timeout_us(i2c, addr, cfg, 2, false, I2C_TIMEOUT_US) != 2)
        return false;

    if (!_write16(dev, LM75A_REG_TOS, _celsius_to_reg(t_os_c))) return false;
    if (!_write16(dev, LM75A_REG_THYST, _celsius_to_reg(t_hyst_c))) return false;
    return true;
}

bool lm75a_read(LM75A *dev, float *temp_c) {
    if (!tca_select(dev->tca, dev->tca_ch)) return false;

    uint8_t reg = LM75A_REG_TEMP;
    uint8_t buf[2];
    if (i2c_write_timeout_us(dev->i2c, dev->addr, &reg, 1,
                             true, I2C_TIMEOUT_US) != 1) return false;
    if (i2c_read_timeout_us(dev->i2c, dev->addr, buf, 2,
                            false, I2C_TIMEOUT_US) != 2) return false;

    // Standard LM75A: 9-bit 2's complement in the upper bits.
    int16_t raw9 = (int16_t)((buf[0] << 8) | buf[1]);
    raw9 >>= 7;
    float t = (float)raw9 * 0.5f;

    if (t < -55.0f || t > 125.0f) return false;
    *temp_c = t;
    return true;
}
