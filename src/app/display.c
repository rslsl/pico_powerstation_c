// ============================================================
// app/display.c — Framebuffer rendering + 8×8 font
// ============================================================
#include "display.h"
#include "config.h"
#include "pico/stdlib.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static uint16_t g_framebuffer[ST7789_W * ST7789_H];
#define ARC_LUT_SIZE 360
#define ARC_LUT_SCALE 1024.0f
static int16_t g_arc_cos_lut[ARC_LUT_SIZE];
static int16_t g_arc_sin_lut[ARC_LUT_SIZE];
static bool g_arc_lut_ready = false;

static void _init_arc_lut(void) {
    if (g_arc_lut_ready) return;
    for (int i = 0; i < ARC_LUT_SIZE; ++i) {
        float a = (float)i * (3.14159265f / 180.0f);
        g_arc_cos_lut[i] = (int16_t)(cosf(a) * ARC_LUT_SCALE);
        g_arc_sin_lut[i] = (int16_t)(sinf(a) * ARC_LUT_SCALE);
    }
    g_arc_lut_ready = true;
}

static void _arc_lut_sample(float deg, float *ca, float *sa) {
    if (!ca || !sa) return;
    if (!isfinite(deg)) {
        *ca = 1.0f;
        *sa = 0.0f;
        return;
    }
    while (deg < 0.0f) deg += 360.0f;
    while (deg >= 360.0f) deg -= 360.0f;
    int idx0 = (int)deg;
    int idx1 = (idx0 + 1) % ARC_LUT_SIZE;
    float frac = deg - (float)idx0;
    float c0 = (float)g_arc_cos_lut[idx0];
    float c1 = (float)g_arc_cos_lut[idx1];
    float s0 = (float)g_arc_sin_lut[idx0];
    float s1 = (float)g_arc_sin_lut[idx1];
    *ca = (c0 + (c1 - c0) * frac) / ARC_LUT_SCALE;
    *sa = (s0 + (s1 - s0) * frac) / ARC_LUT_SCALE;
}

// ── Вбудований мінімальний 8×8 шрифт (ASCII 32–127) ─────────
// Кожен символ: 8 байт, кожен біт = 1 піксель (row-major)
static const uint8_t _FONT8[96][8] = {
    {0,0,0,0,0,0,0,0},        // ' ' 32
    {0x18,0x18,0x18,0x18,0,0,0x18,0},     // '!' 33
    {0x6C,0x6C,0x48,0,0,0,0,0},           // '"' 34
    {0,0x6C,0xFE,0x6C,0x6C,0xFE,0x6C,0}, // '#' 35
    {0x18,0x7E,0x58,0x7E,0x1A,0x7E,0x18,0},// '$'
    {0,0x62,0x64,0x08,0x10,0x26,0x46,0},  // '%'
    {0,0x38,0x44,0x28,0x72,0x44,0x3A,0},  // '&'
    {0x18,0x18,0x10,0,0,0,0,0},           // '\''
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0},// '('
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0},// ')'
    {0,0x28,0x10,0x7C,0x10,0x28,0,0},     // '*'
    {0,0x10,0x10,0x7C,0x10,0x10,0,0},     // '+'
    {0,0,0,0,0x18,0x18,0x10,0x20},        // ','
    {0,0,0,0x7E,0,0,0,0},                 // '-'
    {0,0,0,0,0,0x18,0x18,0},              // '.'
    {0x02,0x04,0x08,0x10,0x20,0x40,0x80,0},// '/'
    // 0-9
    {0x3C,0x46,0x4E,0x56,0x62,0x42,0x3C,0},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0},
    {0x3C,0x42,0x02,0x1C,0x20,0x40,0x7E,0},
    {0x3C,0x42,0x02,0x1C,0x02,0x42,0x3C,0},
    {0x08,0x18,0x28,0x48,0x7E,0x08,0x08,0},
    {0x7E,0x40,0x7C,0x02,0x02,0x42,0x3C,0},
    {0x1C,0x20,0x40,0x7C,0x42,0x42,0x3C,0},
    {0x7E,0x02,0x04,0x08,0x10,0x10,0x10,0},
    {0x3C,0x42,0x42,0x3C,0x42,0x42,0x3C,0},
    {0x3C,0x42,0x42,0x3E,0x02,0x04,0x38,0},
    // ':' ';' '<' '=' '>' '?' '@'
    {0,0,0x18,0,0,0x18,0,0},
    {0,0,0x18,0,0,0x18,0x10,0x20},
    {0x04,0x08,0x10,0x20,0x10,0x08,0x04,0},
    {0,0,0x7E,0,0x7E,0,0,0},
    {0x20,0x10,0x08,0x04,0x08,0x10,0x20,0},
    {0x3C,0x42,0x04,0x08,0x08,0,0x08,0},
    {0x3C,0x42,0x4E,0x56,0x4E,0x40,0x3C,0},
    // A-Z (26)
    {0x18,0x24,0x42,0x7E,0x42,0x42,0x42,0},
    {0x7C,0x42,0x42,0x7C,0x42,0x42,0x7C,0},
    {0x3C,0x42,0x40,0x40,0x40,0x42,0x3C,0},
    {0x78,0x44,0x42,0x42,0x42,0x44,0x78,0},
    {0x7E,0x40,0x40,0x7C,0x40,0x40,0x7E,0},
    {0x7E,0x40,0x40,0x7C,0x40,0x40,0x40,0},
    {0x3C,0x42,0x40,0x4E,0x42,0x42,0x3C,0},
    {0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0},
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x4C,0x38,0},
    {0x42,0x44,0x48,0x70,0x48,0x44,0x42,0},
    {0x40,0x40,0x40,0x40,0x40,0x40,0x7E,0},
    {0x42,0x66,0x5A,0x5A,0x42,0x42,0x42,0},
    {0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0},
    {0x3C,0x42,0x42,0x42,0x42,0x42,0x3C,0},
    {0x7C,0x42,0x42,0x7C,0x40,0x40,0x40,0},
    {0x3C,0x42,0x42,0x42,0x52,0x4A,0x3C,0},
    {0x7C,0x42,0x42,0x7C,0x48,0x44,0x42,0},
    {0x3C,0x42,0x40,0x3C,0x02,0x42,0x3C,0},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0},
    {0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0},
    {0x42,0x42,0x42,0x42,0x24,0x24,0x18,0},
    {0x42,0x42,0x42,0x5A,0x5A,0x66,0x42,0},
    {0x42,0x42,0x24,0x18,0x24,0x42,0x42,0},
    {0x42,0x42,0x24,0x18,0x18,0x18,0x18,0},
    {0x7E,0x02,0x04,0x18,0x20,0x40,0x7E,0},
    // '[' '\' ']' '^' '_' '`'
    {0x38,0x20,0x20,0x20,0x20,0x20,0x38,0},
    {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0},
    {0x38,0x08,0x08,0x08,0x08,0x08,0x38,0},
    {0x10,0x28,0x44,0,0,0,0,0},
    {0,0,0,0,0,0,0,0x7E},
    {0x18,0x10,0x08,0,0,0,0,0},
    // a-z (26)
    {0,0,0x3C,0x02,0x3E,0x42,0x3E,0},
    {0x40,0x40,0x5C,0x62,0x42,0x62,0x5C,0},
    {0,0,0x3C,0x42,0x40,0x42,0x3C,0},
    {0x02,0x02,0x3A,0x46,0x42,0x46,0x3A,0},
    {0,0,0x3C,0x42,0x7E,0x40,0x3C,0},
    {0x0E,0x10,0x7C,0x10,0x10,0x10,0x10,0},
    {0,0,0x3A,0x46,0x46,0x3A,0x02,0x3C},
    {0x40,0x40,0x5C,0x62,0x42,0x42,0x42,0},
    {0x18,0,0x38,0x18,0x18,0x18,0x3C,0},
    {0x04,0,0x0C,0x04,0x04,0x04,0x44,0x38},
    {0x40,0x40,0x44,0x48,0x70,0x48,0x44,0},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0},
    {0,0,0x6C,0x56,0x56,0x42,0x42,0},
    {0,0,0x5C,0x62,0x42,0x42,0x42,0},
    {0,0,0x3C,0x42,0x42,0x42,0x3C,0},
    {0,0,0x5C,0x62,0x62,0x5C,0x40,0x40},
    {0,0,0x3A,0x46,0x46,0x3A,0x02,0x02},
    {0,0,0x5C,0x62,0x40,0x40,0x40,0},
    {0,0,0x3E,0x40,0x3C,0x02,0x7C,0},
    {0x10,0x10,0x7C,0x10,0x10,0x10,0x0E,0},
    {0,0,0x42,0x42,0x42,0x46,0x3A,0},
    {0,0,0x42,0x42,0x42,0x24,0x18,0},
    {0,0,0x42,0x42,0x5A,0x66,0x42,0},
    {0,0,0x42,0x24,0x18,0x24,0x42,0},
    {0,0,0x42,0x42,0x46,0x3A,0x02,0x3C},
    {0,0,0x7E,0x04,0x18,0x20,0x7E,0},
    // '{' '|' '}' '~' DEL
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0},
    {0x18,0x18,0x18,0,0x18,0x18,0x18,0},
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0},
    {0x72,0x9C,0,0,0,0,0,0},
    {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},
};

// ─────────────────────────────────────────────────────────────

void disp_init(Display *d) {
    sleep_ms(DISPLAY_INIT_SETTLE_MS);
    _init_arc_lut();
    st7789_init(d, SPI_PORT,
                SPI_CS_PIN, LCD_DC_PIN, LCD_RST_PIN, LCD_BL_PIN);
    sleep_ms(20);
    disp_fill(d, D_BG);
    disp_flush_sync(d);
    sleep_ms(20);
}

static inline bool _in(int x, int y) {
    return x >= 0 && x < ST7789_W && y >= 0 && y < ST7789_H;
}

static int _safe_left_for_y(int y) {
    if (y < D_SAFE_TOP_CORNERS) return D_SAFE_LEFT + 10;
    if (y > (ST7789_H - D_SAFE_BOTTOM_CORNERS)) return D_SAFE_LEFT + 8;
    return D_SAFE_LEFT;
}

static int _safe_right_for_y(int y) {
    if (y < D_SAFE_TOP_CORNERS) return D_SAFE_RIGHT + 10;
    if (y > (ST7789_H - D_SAFE_BOTTOM_CORNERS)) return D_SAFE_RIGHT + 8;
    return D_SAFE_RIGHT;
}

void disp_fill(Display *d, uint16_t col) {
    (void)d;
    for (int i = 0; i < (ST7789_W * ST7789_H); ++i) g_framebuffer[i] = col;
}

void disp_pixel(Display *d, int x, int y, uint16_t col) {
    (void)d;
    if (!_in(x, y)) return;
    g_framebuffer[y * ST7789_W + x] = col;
}

void disp_hline(Display *d, int x, int y, int w, uint16_t col) {
    (void)d;
    if (w <= 0 || y < 0 || y >= ST7789_H) return;
    if (x < 0) { w += x; x = 0; }
    if ((x + w) > ST7789_W) w = ST7789_W - x;
    if (w <= 0) return;
    uint16_t *row = &g_framebuffer[y * ST7789_W + x];
    for (int i = 0; i < w; ++i) row[i] = col;
}

void disp_vline(Display *d, int x, int y, int h, uint16_t col) {
    (void)d;
    if (h <= 0 || x < 0 || x >= ST7789_W) return;
    if (y < 0) { h += y; y = 0; }
    if ((y + h) > ST7789_H) h = ST7789_H - y;
    if (h <= 0) return;
    for (int i = 0; i < h; ++i) g_framebuffer[(y + i) * ST7789_W + x] = col;
}

void disp_rect(Display *d, int x, int y, int w, int h, uint16_t col) {
    if (w <= 0 || h <= 0) return;
    disp_hline(d, x, y, w, col);
    disp_hline(d, x, y + h - 1, w, col);
    disp_vline(d, x, y, h, col);
    disp_vline(d, x + w - 1, y, h, col);
}

void disp_fill_rect(Display *d, int x, int y, int w, int h, uint16_t col) {
    (void)d;
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if ((x + w) > ST7789_W) w = ST7789_W - x;
    if ((y + h) > ST7789_H) h = ST7789_H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; ++yy) {
        uint16_t *row = &g_framebuffer[(y + yy) * ST7789_W + x];
        for (int xx = 0; xx < w; ++xx) row[xx] = col;
    }
}

void disp_bitmap_1bit(Display *d, int x, int y, int w, int h,
                      const uint8_t *bits, uint16_t col, int scale) {
    if (!bits || w <= 0 || h <= 0) return;
    if (scale < 1) scale = 1;
    int stride = (w + 7) / 8;
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            uint8_t byte = bits[yy * stride + (xx >> 3)];
            if ((byte & (0x80u >> (xx & 7))) == 0) continue;
            disp_fill_rect(d, x + xx * scale, y + yy * scale, scale, scale, col);
        }
    }
}

void disp_char(Display *d, int x, int y, char c, uint16_t col) {
    int idx = (unsigned char)c - 32;
    if (idx < 0 || idx >= 96) idx = 0;
    for (int row = 0; row < 8; row++) {
        uint8_t bits = _FONT8[idx][row];
        for (int col2 = 0; col2 < 8; col2++) {
            if (bits & (0x80u >> col2)) {
                int px = x + col2, py = y + row;
                if (_in(px, py)) g_framebuffer[py * ST7789_W + px] = col;
            }
        }
    }
}

void disp_char2x(Display *d, int x, int y, char c, uint16_t col) {
    int idx = (unsigned char)c - 32;
    if (idx < 0 || idx >= 96) idx = 0;
    for (int row = 0; row < 8; row++) {
        uint8_t bits = _FONT8[idx][row];
        for (int col2 = 0; col2 < 8; col2++) {
            if (bits & (0x80u >> col2)) {
                int px = x + col2 * 2;
                int py = y + row * 2;
                disp_fill_rect(d, px, py, 2, 2, col);
            }
        }
    }
}

void disp_text(Display *d, int x, int y, const char *s, uint16_t col) {
    while (*s) { disp_char(d, x, y, *s++, col); x += 8; }
}

void disp_text2x(Display *d, int x, int y, const char *s, uint16_t col) {
    while (*s) { disp_char2x(d, x, y, *s++, col); x += 16; }
}

void disp_text_safe(Display *d, int x, int y, const char *s, uint16_t col) {
    int safe_x = _safe_left_for_y(y);
    if (x < safe_x) x = safe_x;
    disp_text(d, x, y, s, col);
}

void disp_text_right(Display *d, int y, const char *s, uint16_t col) {
    int len = 0;
    const char *p = s; while (*p++) len++;
    disp_text(d, ST7789_W - len * 8 - 2, y, s, col);
}

void disp_text_right_safe(Display *d, int y, const char *s, uint16_t col) {
    int len = 0;
    const char *p = s; while (*p++) len++;
    int x = ST7789_W - _safe_right_for_y(y) - len * 8;
    if (x < _safe_left_for_y(y)) x = _safe_left_for_y(y);
    disp_text(d, x, y, s, col);
}

void disp_text2x_right_safe(Display *d, int y, const char *s, uint16_t col) {
    int len = 0;
    const char *p = s; while (*p++) len++;
    int x = ST7789_W - _safe_right_for_y(y) - len * 16;
    if (x < _safe_left_for_y(y)) x = _safe_left_for_y(y);
    disp_text2x(d, x, y, s, col);
}

void disp_text_center(Display *d, int y, const char *s, uint16_t col) {
    int len = 0;
    const char *p = s; while (*p++) len++;
    disp_text(d, (ST7789_W - len * 8) / 2, y, s, col);
}

void disp_text_center_safe(Display *d, int y, const char *s, uint16_t col) {
    int len = 0;
    const char *p = s; while (*p++) len++;
    int x = (ST7789_W - len * 8) / 2;
    int left = _safe_left_for_y(y);
    int right = _safe_right_for_y(y);
    if (x < left) x = left;
    if ((x + len * 8) > (ST7789_W - right)) x = ST7789_W - right - len * 8;
    if (x < left) x = left;
    disp_text(d, x, y, s, col);
}

void disp_bar(Display *d, int x, int y, int w, int h,
              float val, float mx, uint16_t col) {
    disp_rect(d, x, y, w, h, D_GRAY);
    if (w <= 2 || h <= 2) return;
    if (!isfinite(val) || !isfinite(mx) || mx <= 0.0f) return;
    int filled = (int)(((float)(w - 2) * (val / mx)) + 0.5f);
    if (filled < 0) filled = 0;
    if (filled > (w - 2)) filled = (w - 2);
    if (filled > 0) disp_fill_rect(d, x + 1, y + 1, filled, h - 2, col);
}

void disp_ring(Display *d, int cx, int cy, int r, int thick,
               float fraction, uint16_t col_fg, uint16_t col_bg) {
    float step = 0.02f;
    for (float angle = -M_PI_2;
         angle < 2.0f * M_PI - M_PI_2; angle += step) {
        bool filled = angle < (-M_PI_2 + fraction * 2.0f * M_PI);
        uint16_t col = filled ? col_fg : col_bg;
        float ca = cosf(angle), sa = sinf(angle);
        for (int t = 0; t < thick; t++) {
            int rr = r - t;
            int px = cx + (int)(rr * ca);
            int py = cy + (int)(rr * sa);
            disp_pixel(d, px, py, col);
        }
    }
}

void disp_ring_arc(Display *d, int cx, int cy, int r, int thick,
                   float start_deg, float sweep_deg, float frac,
                   uint16_t col_fg, uint16_t col_bg) {
    if (r <= 0 || thick <= 0 || sweep_deg <= 0.0f) return;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int steps = (int)(sweep_deg * (float)r * 0.0174533f);
    if (steps < (int)sweep_deg) steps = (int)sweep_deg;
    if (steps < 1) steps = 1;
    _init_arc_lut();
    for (int step_i = 0; step_i <= steps; ++step_i) {
        float a_frac = (float)step_i / (float)steps;
        float angle_deg = start_deg + sweep_deg * a_frac;
        uint16_t col = (a_frac <= frac) ? col_fg : col_bg;
        float ca, sa;
        _arc_lut_sample(angle_deg, &ca, &sa);
        for (int t = 0; t < thick; t++) {
            int rr = r - t;
            if (rr <= 0) break;
            int px = cx + (int)(rr * ca + 0.5f);
            int py = cy + (int)(rr * sa + 0.5f);
            disp_pixel(d, px, py, col);
        }
    }
}

void disp_header(Display *d, const char *title, const char *right) {
    disp_fill_rect(d, 8, 6, ST7789_W - 16, 18, 0x08A6u);
    disp_rect(d, 6, 4, ST7789_W - 12, 22, 0x11D8u);
    disp_rect(d, 7, 5, ST7789_W - 14, 20, D_ACCENT);
    disp_hline(d, 18, 10, 40, D_ACCENT);
    disp_hline(d, ST7789_W - 58, 10, 40, D_ACCENT);
    disp_text_safe(d, D_SAFE_LEFT + 2, 11, title, D_WHITE);
    if (right) disp_text_right_safe(d, 11, right, D_SUBTEXT);
}

void disp_footer(Display *d, const char *left, const char *right) {
    int y = ST7789_H - 24;
    disp_fill_rect(d, 8, y, ST7789_W - 16, 16, 0x08A6u);
    disp_rect(d, 6, y - 2, ST7789_W - 12, 20, 0x11D8u);
    disp_rect(d, 7, y - 1, ST7789_W - 14, 18, D_ACCENT);
    disp_text_safe(d, D_SAFE_LEFT + 2, y + 4, left, D_WHITE);
    if (right) disp_text_right_safe(d, y + 4, right, D_SUBTEXT);
}

void disp_dialog(Display *d, const char *lines[], int n) {
    int box_h = n * 12 + 16;
    int box_y = (ST7789_H - box_h) / 2;
    int box_x = D_SAFE_LEFT + 4;
    int box_w = ST7789_W - (D_SAFE_LEFT + D_SAFE_RIGHT + 8);
    disp_fill_rect(d, box_x, box_y, box_w, box_h, 0x08A6u);
    disp_rect(d, box_x - 1, box_y - 1, box_w + 2, box_h + 2, 0x11D8u);
    disp_rect(d, box_x, box_y, box_w, box_h, D_ACCENT);
    for (int i = 0; i < n; i++)
        disp_text_center_safe(d, box_y + 8 + i * 12, lines[i], D_WHITE);
}

// Current ST7789 backend is synchronous: draw_rgb565() blocks until the frame
// is pushed, so flush_wait() is intentionally a no-op for this build.
void disp_flush(Display *d)      { st7789_draw_rgb565(d, 0, 0, ST7789_W, ST7789_H, g_framebuffer); }
void disp_flush_wait(Display *d) { st7789_wait_flush(d); }
void disp_flush_sync(Display *d) { disp_flush(d); disp_flush_wait(d); }
