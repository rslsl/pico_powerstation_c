#pragma once
// ============================================================
// drivers/tca9548a.h — I2C Multiplexer TCA9548A
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

typedef struct {
    i2c_inst_t *i2c;
    uint8_t     addr;
    int8_t      current_ch;   // -1 = невідомий стан
} TCA9548A;

// Ініціалізація (I2C вже налаштований)
void tca_init(TCA9548A *dev, i2c_inst_t *i2c, uint8_t addr);

// Вибір каналу 0-7. Повертає true при успіху.
bool tca_select(TCA9548A *dev, uint8_t ch);

// Вимкнути всі канали
bool tca_disable(TCA9548A *dev);

// Сканування: заповнює found[8] списками знайдених адрес.
// Повертає кількість знайдених пристроїв.
int  tca_scan(TCA9548A *dev, uint8_t found[8][16], uint8_t counts[8]);

// I2C bus recovery: 9 clock pulses на SCL для звільнення застряглого slave.
// Викликати при I2C_TIMEOUT або NACK від пристрою.
bool i2c_bus_recover(i2c_inst_t *i2c, uint8_t sda_pin, uint8_t scl_pin);
