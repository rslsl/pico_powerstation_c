#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"

// New pinout under test
#define I2C_PORT_USED i2c0
#define I2C_SDA_PIN 0
#define I2C_SCL_PIN 1
#define I2C_BAUD_HZ 400000

#define SPI_PORT_USED spi0
#define SPI_SCK_PIN 18
#define SPI_MOSI_PIN 19
#define SPI_BAUD_HZ 8000000

#define LCD_CS_PIN 17
#define LCD_DC_PIN 20
#define LCD_RST_PIN 21
#define LCD_BL_PIN 22

#define BTN_UP_PIN 9
#define BTN_OK_PIN 10
#define BTN_DOWN_PIN 11

// Active-low power outputs
#define GPIO_DC_OUT_PIN 12
#define GPIO_USB4_PIN 13
#define GPIO_USB_PD_PIN 14
#define GPIO_INVERTER_PIN 15
#define GPIO_CHARGE_IN_PIN 16

#define LCD_WIDTH 240
#define LCD_HEIGHT 280
#define LCD_X_OFFSET 0
#define LCD_Y_OFFSET 20

#define TCA9548A_ADDR 0x70
#define INA226_DIS_ADDR 0x40
#define INA226_CHG_ADDR 0x41
#define INA3221_ADDR 0x43
#define LM75_BAT_ADDR 0x48
#define LM75_INV_ADDR 0x49

static inline void cs_select(void) { gpio_put(LCD_CS_PIN, 0); }
static inline void cs_deselect(void) { gpio_put(LCD_CS_PIN, 1); }
static inline void dc_command(void) { gpio_put(LCD_DC_PIN, 0); }
static inline void dc_data(void) { gpio_put(LCD_DC_PIN, 1); }

static void st7789_write_cmd(uint8_t cmd) {
    dc_command();
    cs_select();
    spi_write_blocking(SPI_PORT_USED, &cmd, 1);
    cs_deselect();
}

static void st7789_write_data(const uint8_t *data, size_t len) {
    dc_data();
    cs_select();
    spi_write_blocking(SPI_PORT_USED, data, len);
    cs_deselect();
}

static void st7789_reset(void) {
    gpio_put(LCD_RST_PIN, 1);
    sleep_ms(10);
    gpio_put(LCD_RST_PIN, 0);
    sleep_ms(20);
    gpio_put(LCD_RST_PIN, 1);
    sleep_ms(120);
}

static void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t buf[4];
    x0 += LCD_X_OFFSET; x1 += LCD_X_OFFSET;
    y0 += LCD_Y_OFFSET; y1 += LCD_Y_OFFSET;

    st7789_write_cmd(0x2A);
    buf[0] = (uint8_t)(x0 >> 8); buf[1] = (uint8_t)(x0 & 0xFF);
    buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)(x1 & 0xFF);
    st7789_write_data(buf, 4);

    st7789_write_cmd(0x2B);
    buf[0] = (uint8_t)(y0 >> 8); buf[1] = (uint8_t)(y0 & 0xFF);
    buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)(y1 & 0xFF);
    st7789_write_data(buf, 4);

    st7789_write_cmd(0x2C);
}

static void st7789_fill_color(uint16_t color) {
    st7789_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    const uint8_t hi = (uint8_t)(color >> 8);
    const uint8_t lo = (uint8_t)(color & 0xFF);

    uint8_t linebuf[LCD_WIDTH * 2];
    for (int i = 0; i < LCD_WIDTH; ++i) {
        linebuf[2 * i] = hi;
        linebuf[2 * i + 1] = lo;
    }

    dc_data();
    cs_select();
    for (int y = 0; y < LCD_HEIGHT; ++y) {
        spi_write_blocking(SPI_PORT_USED, linebuf, sizeof(linebuf));
    }
    cs_deselect();
}

static void st7789_init_min(void) {
    spi_init(SPI_PORT_USED, SPI_BAUD_HZ);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI_PIN, GPIO_FUNC_SPI);

    gpio_init(LCD_CS_PIN);
    gpio_set_dir(LCD_CS_PIN, GPIO_OUT);
    cs_deselect();

    gpio_init(LCD_DC_PIN);
    gpio_set_dir(LCD_DC_PIN, GPIO_OUT);
    gpio_put(LCD_DC_PIN, 1);

    gpio_init(LCD_RST_PIN);
    gpio_set_dir(LCD_RST_PIN, GPIO_OUT);

    gpio_set_function(LCD_BL_PIN, GPIO_FUNC_PWM);
    uint bl_slice = pwm_gpio_to_slice_num(LCD_BL_PIN);
    pwm_set_wrap(bl_slice, 255);
    pwm_set_gpio_level(LCD_BL_PIN, 255);
    pwm_set_enabled(bl_slice, true);

    st7789_reset();

    st7789_write_cmd(0x01); // SWRESET
    sleep_ms(150);
    st7789_write_cmd(0x11); // SLPOUT
    sleep_ms(120);

    uint8_t colmod = 0x55;
    st7789_write_cmd(0x3A); // COLMOD
    st7789_write_data(&colmod, 1);

    uint8_t madctl = 0x00;
    st7789_write_cmd(0x36); // MADCTL
    st7789_write_data(&madctl, 1);

    st7789_write_cmd(0x21); // INVON
    sleep_ms(10);
    st7789_write_cmd(0x13); // NORON
    sleep_ms(10);
    st7789_write_cmd(0x29); // DISPON
    sleep_ms(50);
}

static void outputs_safe_off(void) {
    const uint pins[] = {
        GPIO_DC_OUT_PIN, GPIO_USB4_PIN, GPIO_USB_PD_PIN, GPIO_INVERTER_PIN, GPIO_CHARGE_IN_PIN
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 1); // active-low OFF
    }
}

static void buttons_init(void) {
    gpio_init(BTN_UP_PIN); gpio_set_dir(BTN_UP_PIN, GPIO_IN); gpio_pull_up(BTN_UP_PIN);
    gpio_init(BTN_OK_PIN); gpio_set_dir(BTN_OK_PIN, GPIO_IN); gpio_pull_up(BTN_OK_PIN);
    gpio_init(BTN_DOWN_PIN); gpio_set_dir(BTN_DOWN_PIN, GPIO_IN); gpio_pull_up(BTN_DOWN_PIN);
}

static bool button_pressed(uint pin) { return gpio_get(pin) == 0; }

static void i2c_bus_init(void) {
    i2c_init(I2C_PORT_USED, I2C_BAUD_HZ);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
}

static bool i2c_probe_addr(uint8_t addr) {
    uint8_t dummy = 0;
    int rc = i2c_write_timeout_us(I2C_PORT_USED, addr, &dummy, 0, false, 3000);
    return rc >= 0;
}

static void print_probe(const char *name, uint8_t addr) {
    printf("%-12s 0x%02X : %s\n", name, addr, i2c_probe_addr(addr) ? "OK" : "MISS");
}

int main(void) {
    stdio_init_all();

    // Give USB stack a moment. If host is attached, CDC comes up here.
    sleep_ms(1200);
    for (int i = 0; i < 40 && !stdio_usb_connected(); ++i) {
        sleep_ms(100);
    }

    printf("\n=== Pico periph test (new pinout) ===\n");
    printf("SPI: SCK=%u MOSI=%u CS=%u DC=%u RST=%u BL=%u\n",
           SPI_SCK_PIN, SPI_MOSI_PIN, LCD_CS_PIN, LCD_DC_PIN, LCD_RST_PIN, LCD_BL_PIN);
    printf("I2C: SDA=%u SCL=%u @ %u Hz\n", I2C_SDA_PIN, I2C_SCL_PIN, I2C_BAUD_HZ);

    outputs_safe_off();
    buttons_init();
    i2c_bus_init();
    st7789_init_min();

    st7789_fill_color(0xF800); printf("Display RED\n"); sleep_ms(600);
    st7789_fill_color(0x07E0); printf("Display GREEN\n"); sleep_ms(600);
    st7789_fill_color(0x001F); printf("Display BLUE\n"); sleep_ms(600);
    st7789_fill_color(0xFFFF); printf("Display WHITE\n"); sleep_ms(600);
    st7789_fill_color(0x0000); printf("Display BLACK\n");

    printf("\nI2C expected devices:\n");
    print_probe("TCA9548A", TCA9548A_ADDR);
    print_probe("INA226 DIS", INA226_DIS_ADDR);
    print_probe("INA226 CHG", INA226_CHG_ADDR);
    print_probe("INA3221", INA3221_ADDR);
    print_probe("LM75 BAT", LM75_BAT_ADDR);
    print_probe("LM75 INV", LM75_INV_ADDR);

    bool prev_up = false, prev_ok = false, prev_down = false;
    while (true) {
        bool up = button_pressed(BTN_UP_PIN);
        bool ok = button_pressed(BTN_OK_PIN);
        bool down = button_pressed(BTN_DOWN_PIN);
        if (up != prev_up || ok != prev_ok || down != prev_down) {
            printf("Buttons: UP=%d OK=%d DOWN=%d\n", up ? 1 : 0, ok ? 1 : 0, down ? 1 : 0);
            prev_up = up;
            prev_ok = ok;
            prev_down = down;
        }
        sleep_ms(50);
    }
}
