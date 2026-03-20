#pragma once
// ============================================================
// app/display.h - immediate-mode graphics layer over ST7789
// ============================================================
#include <stdbool.h>
#include <stdint.h>
#include "../drivers/st7789.h"

#define D_BG        0x0820u
#define D_TEXT      0xC6D8u
#define D_SUBTEXT   0x4ACBu
#define D_ACCENT    0x07FFu
#define D_GREEN     0x07E0u
#define D_YELLOW    0xFFE0u
#define D_ORANGE    0xFC00u
#define D_RED       0xF800u
#define D_PURPLE    0x981Fu
#define D_WHITE     0xFFFFu
#define D_GRAY      0x4208u

#define D_SAFE_LEFT           12
#define D_SAFE_RIGHT          12
#define D_SAFE_TOP            6
#define D_SAFE_BOTTOM         6
#define D_SAFE_TOP_CORNERS    20
#define D_SAFE_BOTTOM_CORNERS 18

typedef ST7789 Display;

void disp_init(Display *d);

void disp_fill(Display *d, uint16_t col);
void disp_pixel(Display *d, int x, int y, uint16_t col);
void disp_hline(Display *d, int x, int y, int w, uint16_t col);
void disp_vline(Display *d, int x, int y, int h, uint16_t col);
void disp_rect(Display *d, int x, int y, int w, int h, uint16_t col);
void disp_fill_rect(Display *d, int x, int y, int w, int h, uint16_t col);
void disp_bitmap_1bit(Display *d, int x, int y, int w, int h,
                      const uint8_t *bits, uint16_t col, int scale);

void disp_char(Display *d, int x, int y, char c, uint16_t col);
void disp_text(Display *d, int x, int y, const char *s, uint16_t col);
void disp_char2x(Display *d, int x, int y, char c, uint16_t col);
void disp_text2x(Display *d, int x, int y, const char *s, uint16_t col);
void disp_text2x_right_safe(Display *d, int y, const char *s, uint16_t col);
void disp_text_right(Display *d, int y, const char *s, uint16_t col);
void disp_text_center(Display *d, int y, const char *s, uint16_t col);
void disp_text_safe(Display *d, int x, int y, const char *s, uint16_t col);
void disp_text_right_safe(Display *d, int y, const char *s, uint16_t col);
void disp_text_center_safe(Display *d, int y, const char *s, uint16_t col);

void disp_bar(Display *d, int x, int y, int w, int h,
              float val, float mx, uint16_t col);
void disp_ring(Display *d, int cx, int cy, int r, int thick,
               float fraction, uint16_t col_fg, uint16_t col_bg);
// Partial-arc ring gauge: sweeps sweep_deg degrees starting at start_deg
// (0=right, 90=down in screen coords). frac in [0,1] sets filled portion.
void disp_ring_arc(Display *d, int cx, int cy, int r, int thick,
                   float start_deg, float sweep_deg, float frac,
                   uint16_t col_fg, uint16_t col_bg);

void disp_header(Display *d, const char *title, const char *right);
void disp_footer(Display *d, const char *left, const char *right);
void disp_dialog(Display *d, const char *lines[], int n);

// Immediate-mode backend keeps these for API compatibility.
void disp_flush(Display *d);
void disp_flush_wait(Display *d);
void disp_flush_sync(Display *d);
