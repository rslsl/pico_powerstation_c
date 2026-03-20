// ============================================================
// drivers/tca9548a.c — TCA9548A I2C Multiplexer
// ============================================================
#include "tca9548a.h"
#include "config.h"
#include "pico/stdlib.h"
#include <stdio.h>

void tca_init(TCA9548A *dev, i2c_inst_t *i2c, uint8_t addr) {
    dev->i2c        = i2c;
    dev->addr       = addr;
    dev->current_ch = -1;
    tca_disable(dev);
}

bool tca_select(TCA9548A *dev, uint8_t ch) {
    if (ch > 7) return false;
    if (dev->current_ch == (int8_t)ch) return true;  // вже вибраний

    uint8_t reg = (uint8_t)(1u << ch);
    int rc = i2c_write_timeout_us(dev->i2c, dev->addr,
                                  &reg, 1, false, I2C_TIMEOUT_US);
    if (rc != 1) {
        printf("[TCA] select ch%d failed: %d\n", ch, rc);
        dev->current_ch = -1;
        return false;
    }
    dev->current_ch = (int8_t)ch;
    return true;
}

bool tca_disable(TCA9548A *dev) {
    uint8_t reg = 0x00;
    int rc = i2c_write_timeout_us(dev->i2c, dev->addr,
                                  &reg, 1, false, I2C_TIMEOUT_US);
    dev->current_ch = -1;
    return rc == 1;
}

int tca_scan(TCA9548A *dev, uint8_t found[8][16], uint8_t counts[8]) {
    int total = 0;
    for (int ch = 0; ch < 8; ch++) {
        counts[ch] = 0;
        if (!tca_select(dev, ch)) continue;
        // Сканування адрес 0x08..0x77
        for (uint8_t addr = 0x08; addr < 0x78; addr++) {
            uint8_t dummy;
            int rc = i2c_read_timeout_us(dev->i2c, addr,
                                         &dummy, 1, false, I2C_TIMEOUT_US);
            if (rc == 1) {
                if (counts[ch] < 16) {
                    found[ch][counts[ch]++] = addr;
                    total++;
                }
            }
        }
    }
    tca_disable(dev);
    return total;
}

// ── I2C Bus Recovery (#4) ─────────────────────────────────────
// При I2C lock-up (slave тримає SDA низько): переводимо SCL у GPIO,
// генеруємо 9 clock pulses — більшість I2C slaves розблокуються.
bool i2c_bus_recover(i2c_inst_t *i2c, uint8_t sda_pin, uint8_t scl_pin) {
    // Відключити I2C периферію
    i2c_deinit(i2c);

    // SCL і SDA — у GPIO режим
    gpio_set_function(scl_pin, GPIO_FUNC_SIO);
    gpio_set_function(sda_pin, GPIO_FUNC_SIO);
    gpio_set_dir(scl_pin, GPIO_OUT);
    gpio_set_dir(sda_pin, GPIO_IN);
    gpio_put(scl_pin, 1);

    // 9 clock pulses (мінімум для будь-якого I2C slave)
    for (int i = 0; i < 9; i++) {
        gpio_put(scl_pin, 0); sleep_us(5);
        gpio_put(scl_pin, 1); sleep_us(5);
        if (gpio_get(sda_pin)) break;   // SDA вільна — достатньо
    }

    // STOP condition: SDA low → high при SCL high
    gpio_set_dir(sda_pin, GPIO_OUT);
    gpio_put(sda_pin, 0); sleep_us(5);
    gpio_put(scl_pin, 1); sleep_us(5);
    gpio_put(sda_pin, 1); sleep_us(5);

    // Повернути в I2C режим
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);
    i2c_init(i2c, I2C_FREQ_HZ);

    bool ok = gpio_get(sda_pin);   // SDA має бути HIGH після recovery
    printf("[I2C] bus recovery: SDA=%d\n", ok);
    return ok;
}
