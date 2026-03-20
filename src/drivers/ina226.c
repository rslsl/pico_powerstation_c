// ============================================================
// drivers/ina226.c — INA226 Current/Power Monitor
// ============================================================
#include "ina226.h"
#include "config.h"
#include <stdio.h>
#include <math.h>

static bool _write_reg(INA226 *dev, uint8_t reg, uint16_t val) {
    uint8_t buf[3] = {reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    return i2c_write_timeout_us(dev->i2c, dev->addr, buf, 3,
                                false, I2C_TIMEOUT_US) == 3;
}

static bool _read_reg(INA226 *dev, uint8_t reg, int16_t *out) {
    uint8_t buf[2];
    if (i2c_write_timeout_us(dev->i2c, dev->addr, &reg, 1,
                             true, I2C_TIMEOUT_US) != 1) return false;
    if (i2c_read_timeout_us(dev->i2c, dev->addr, buf, 2,
                            false, I2C_TIMEOUT_US) != 2) return false;
    *out = (int16_t)((buf[0] << 8) | buf[1]);
    return true;
}

bool ina226_init(INA226 *dev, i2c_inst_t *i2c, TCA9548A *tca,
                 uint8_t tca_ch, uint8_t addr,
                 float shunt_mohm, float i_max_a) {
    dev->i2c        = i2c;
    dev->tca        = tca;
    dev->tca_ch     = tca_ch;
    dev->addr       = addr;
    dev->shunt_mohm = shunt_mohm;

    // LSB струму: I_max / 2^15 (A/bit)
    dev->lsb_current_a = i_max_a / 32768.0f;

    if (!tca_select(dev->tca, dev->tca_ch)) return false;

    // Config: 16 averages, 1.1ms Vbus, 1.1ms Vshunt, continuous
    // BADC=SADC=0b1000 (1.1ms), AVG=0b100 (16x) → 0x4527
    if (!_write_reg(dev, INA226_REG_CFG, 0x4527)) return false;

    // Калібровка: Cal = 0.00512 / (LSB_I * R_shunt_ohm)
    float r_ohm = shunt_mohm / 1000.0f;
    uint16_t cal = (uint16_t)(0.00512f / (dev->lsb_current_a * r_ohm));
    if (!_write_reg(dev, INA226_REG_CAL, cal)) return false;

    printf("[INA226] ch%d addr=0x%02X cal=%u lsb=%.6fA\n",
           tca_ch, addr, cal, dev->lsb_current_a);
    return true;
}

bool ina226_read(INA226 *dev, float *v_bus_v, float *i_a, float *p_w) {
    if (!tca_select(dev->tca, dev->tca_ch)) return false;

    int16_t raw_v, raw_i;
    if (!_read_reg(dev, INA226_REG_BUS,     &raw_v)) return false;
    if (!_read_reg(dev, INA226_REG_CURRENT, &raw_i)) return false;

    // Vbus LSB = 1.25 мВ
    float v = (float)((uint16_t)raw_v) * 0.00125f;
    float i = (float)raw_i * dev->lsb_current_a;
    float p = v * fabsf(i);

    // Sanity check
    if (v < 5.0f || v > 20.0f) return false;

    *v_bus_v = v;
    *i_a     = i;
    *p_w     = p;
    return true;
}
