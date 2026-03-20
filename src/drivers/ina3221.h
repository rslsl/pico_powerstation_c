#pragma once
// ============================================================
// drivers/ina3221.h — INA3221 3-Channel Current Monitor
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"
#include "tca9548a.h"

#define INA3221_REG_CFG     0x00
#define INA3221_REG_SH1     0x01  // Shunt ch1
#define INA3221_REG_BUS1    0x02  // Bus ch1
#define INA3221_REG_SH2     0x03
#define INA3221_REG_BUS2    0x04
#define INA3221_REG_SH3     0x05
#define INA3221_REG_BUS3    0x06

typedef struct {
    i2c_inst_t *i2c;
    TCA9548A   *tca;
    uint8_t     tca_ch;
    uint8_t     addr;
} INA3221;

bool ina3221_init(INA3221 *dev, i2c_inst_t *i2c, TCA9548A *tca,
                  uint8_t tca_ch, uint8_t addr);

// Повертає напруги B1/B2/B3 та максимальний дисбаланс (мВ)
bool ina3221_read_cells(INA3221 *dev,
                        float *v_b1, float *v_b2, float *v_b3,
                        float *delta_mv);
