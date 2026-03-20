#include "st7789.h"

#include "../config.h"
#include "../third_party/ST7789_TFT_PICO/include/st7789/ST7789_TFT.hpp"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

struct ST7789Impl {
    ST7789_TFT tft;
    uint8_t backlight_pin;
    bool backlight_enabled;
};

static ST7789Impl *st_impl(ST7789 *dev) {
    return static_cast<ST7789Impl *>(dev->impl);
}

static ST7789_TFT_graphics::TFT_Font_Type_e kDefaultFont =
    ST7789_TFT_graphics::TFTFont_Default;

extern "C" {

void st7789_init(ST7789 *dev, spi_inst_t *spi,
                 uint8_t cs, uint8_t dc, uint8_t rst, uint8_t bl) {
    (void)bl;
    if (!dev) return;

    dev->impl = new ST7789Impl();
    ST7789Impl *impl = st_impl(dev);
    impl->backlight_pin = bl;
    impl->backlight_enabled = (bl != 255u);

    impl->tft.TFTInitSPIType(SPI_BAUD_HZ / 1000, spi);
    impl->tft.TFTSetupGPIO(rst, dc, cs, SPI_SCK_PIN, SPI_MOSI_PIN);
    impl->tft.TFTInitScreenSize(LCD_X_OFFSET, LCD_Y_OFFSET, ST7789_W, ST7789_H);
    impl->tft.TFTST7789Initialize();
    impl->tft.TFTFontNum(kDefaultFont);
    impl->tft.TFTsetTextWrap(false);

    if (impl->backlight_enabled) {
        gpio_set_function(bl, GPIO_FUNC_PWM);
        uint slice = pwm_gpio_to_slice_num(bl);
        pwm_set_wrap(slice, 255);
        pwm_set_enabled(slice, true);
        st7789_set_brightness(dev, 100);
    }
}

void st7789_set_brightness(ST7789 *dev, uint8_t pct) {
    if (!dev || !dev->impl) return;
    ST7789Impl *impl = st_impl(dev);
    if (!impl->backlight_enabled) return;

    if (pct > 100u) pct = 100u;
    uint slice = pwm_gpio_to_slice_num(impl->backlight_pin);
    uint chan = pwm_gpio_to_channel(impl->backlight_pin);
    uint16_t level = (uint16_t)((pct * 255u) / 100u);
    pwm_set_chan_level(slice, chan, level);
}

void st7789_fill_screen(ST7789 *dev, uint16_t color) {
    if (!dev || !dev->impl) return;
    st_impl(dev)->tft.TFTfillScreen(color);
}

void st7789_draw_pixel(ST7789 *dev, int x, int y, uint16_t color) {
    if (!dev || !dev->impl || x < 0 || y < 0) return;
    st_impl(dev)->tft.TFTdrawPixel((uint16_t)x, (uint16_t)y, color);
}

void st7789_draw_fast_hline(ST7789 *dev, int x, int y, int w, uint16_t color) {
    if (!dev || !dev->impl || x < 0 || y < 0 || w <= 0) return;
    st_impl(dev)->tft.TFTdrawFastHLine((uint16_t)x, (uint16_t)y, (uint16_t)w, color);
}

void st7789_draw_fast_vline(ST7789 *dev, int x, int y, int h, uint16_t color) {
    if (!dev || !dev->impl || x < 0 || y < 0 || h <= 0) return;
    st_impl(dev)->tft.TFTdrawFastVLine((uint16_t)x, (uint16_t)y, (uint16_t)h, color);
}

void st7789_draw_rect(ST7789 *dev, int x, int y, int w, int h, uint16_t color) {
    if (!dev || !dev->impl || x < 0 || y < 0 || w <= 0 || h <= 0) return;
    st_impl(dev)->tft.TFTdrawRectWH((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
}

void st7789_fill_rect(ST7789 *dev, int x, int y, int w, int h, uint16_t color) {
    if (!dev || !dev->impl || x < 0 || y < 0 || w <= 0 || h <= 0) return;
    st_impl(dev)->tft.TFTfillRectBuffer((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, color);
}

void st7789_draw_rgb565(ST7789 *dev, int x, int y, int w, int h, const uint16_t *pixels) {
    if (!dev || !dev->impl || !pixels || x < 0 || y < 0 || w <= 0 || h <= 0) return;
    static uint8_t rowbuf[ST7789_W * 2];
    ST7789_TFT &tft = st_impl(dev)->tft;
    for (int row = 0; row < h; ++row) {
        const uint16_t *src = pixels + row * w;
        for (int i = 0; i < w; ++i) {
            uint16_t c = src[i];
            rowbuf[2 * i] = (uint8_t)(c >> 8);
            rowbuf[2 * i + 1] = (uint8_t)(c & 0xFF);
        }
        tft.TFTdrawBitmap16Data((uint16_t)x, (uint16_t)(y + row), rowbuf, (uint16_t)w, 1);
    }
}

void st7789_flush_async(ST7789 *dev) {
    (void)dev;
}

void st7789_wait_flush(ST7789 *dev) {
    (void)dev;
}

void st7789_flush_sync(ST7789 *dev) {
    (void)dev;
}

}
