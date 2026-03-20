#pragma once
// ============================================================
// drivers/lm75a.h — LM75A Temperature Sensor
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"
#include "tca9548a.h"

typedef struct {
    i2c_inst_t *i2c;
    TCA9548A   *tca;
    uint8_t     tca_ch;
    uint8_t     addr;
} LM75A;

bool  lm75a_init(LM75A *dev, i2c_inst_t *i2c, TCA9548A *tca,
                 uint8_t tca_ch, uint8_t addr,
                 float t_os_c, float t_hyst_c);

// Повертає false при I2C помилці. temp_c не змінюється при помилці.
bool  lm75a_read(LM75A *dev, float *temp_c);
