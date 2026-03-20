#pragma once
// ============================================================
// drivers/ina226.h — INA226 Current/Power Monitor
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"
#include "tca9548a.h"

// Регістри INA226
#define INA226_REG_CFG      0x00
#define INA226_REG_SHUNT    0x01
#define INA226_REG_BUS      0x02
#define INA226_REG_POWER    0x03
#define INA226_REG_CURRENT  0x04
#define INA226_REG_CAL      0x05

typedef struct {
    i2c_inst_t *i2c;
    TCA9548A   *tca;
    uint8_t     tca_ch;
    uint8_t     addr;
    float       lsb_current_a;   // A/bit після калібровки
    float       shunt_mohm;
} INA226;

bool  ina226_init(INA226 *dev, i2c_inst_t *i2c, TCA9548A *tca,
                  uint8_t tca_ch, uint8_t addr,
                  float shunt_mohm, float i_max_a);

// Повертає false при I2C помилці
bool  ina226_read(INA226 *dev,
                  float *v_bus_v,
                  float *i_a,
                  float *p_w);
