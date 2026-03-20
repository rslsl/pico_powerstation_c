#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "hardware/spi.h"

#define ST7789_W 240
#define ST7789_H 280

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *impl;
} ST7789;

void st7789_init(ST7789 *dev, spi_inst_t *spi,
                 uint8_t cs, uint8_t dc, uint8_t rst, uint8_t bl);
void st7789_set_brightness(ST7789 *dev, uint8_t pct);
void st7789_fill_screen(ST7789 *dev, uint16_t color);
void st7789_draw_pixel(ST7789 *dev, int x, int y, uint16_t color);
void st7789_draw_fast_hline(ST7789 *dev, int x, int y, int w, uint16_t color);
void st7789_draw_fast_vline(ST7789 *dev, int x, int y, int h, uint16_t color);
void st7789_draw_rect(ST7789 *dev, int x, int y, int w, int h, uint16_t color);
void st7789_fill_rect(ST7789 *dev, int x, int y, int w, int h, uint16_t color);
void st7789_draw_rgb565(ST7789 *dev, int x, int y, int w, int h, const uint16_t *pixels);
void st7789_flush_async(ST7789 *dev);
void st7789_wait_flush(ST7789 *dev);
void st7789_flush_sync(ST7789 *dev);

#ifdef __cplusplus
}
#endif
