// graphics.c
#include "graphics.h"
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "font_cyrillic.h"

static esp_lcd_panel_handle_t lcd_panel;
static int lcd_width;
static int lcd_height;

// Простой шрифт 5x7 (ASCII 32-126)
static const uint8_t font5x7[] = {
    0x00,0x00,0x00,0x00,0x00, // space
    0x00,0x00,0x5F,0x00,0x00, // !
    0x00,0x07,0x00,0x07,0x00, // "
    0x14,0x7F,0x14,0x7F,0x14, // #
    0x24,0x2A,0x7F,0x2A,0x12, // $
    0x23,0x13,0x08,0x64,0x62, // %
    0x36,0x49,0x55,0x22,0x50, // &
    0x00,0x05,0x03,0x00,0x00, // '
    0x00,0x1C,0x22,0x41,0x00, // (
    0x00,0x41,0x22,0x1C,0x00, // )
    0x08,0x2A,0x1C,0x2A,0x08, // *
    0x08,0x08,0x3E,0x08,0x08, // +
    0x00,0x50,0x30,0x00,0x00, // ,
    0x08,0x08,0x08,0x08,0x08, // -
    0x00,0x60,0x60,0x00,0x00, // .
    0x20,0x10,0x08,0x04,0x02, // /
    0x3E,0x51,0x49,0x45,0x3E, // 0
    0x00,0x42,0x7F,0x40,0x00, // 1
    0x42,0x61,0x51,0x49,0x46, // 2
    0x21,0x41,0x45,0x4B,0x31, // 3
    0x18,0x14,0x12,0x7F,0x10, // 4
    0x27,0x45,0x45,0x45,0x39, // 5
    0x3C,0x4A,0x49,0x49,0x30, // 6
    0x01,0x71,0x09,0x05,0x03, // 7
    0x36,0x49,0x49,0x49,0x36, // 8
    0x06,0x49,0x49,0x29,0x1E, // 9
    0x00,0x36,0x36,0x00,0x00, // :
    0x00,0x56,0x36,0x00,0x00, // ;
    0x00,0x08,0x14,0x22,0x41, // 
    0x14,0x14,0x14,0x14,0x14, // =
    0x41,0x22,0x14,0x08,0x00, // >
    0x02,0x01,0x51,0x09,0x06, // ?
    0x32,0x49,0x79,0x41,0x3E, // @
    0x7E,0x11,0x11,0x11,0x7E, // A
    0x7F,0x49,0x49,0x49,0x36, // B
    0x3E,0x41,0x41,0x41,0x22, // C
    0x7F,0x41,0x41,0x22,0x1C, // D
    0x7F,0x49,0x49,0x49,0x41, // E
    0x7F,0x09,0x09,0x01,0x01, // F
    0x3E,0x41,0x41,0x51,0x32, // G
    0x7F,0x08,0x08,0x08,0x7F, // H
    0x00,0x41,0x7F,0x41,0x00, // I
    0x20,0x40,0x41,0x3F,0x01, // J
    0x7F,0x08,0x14,0x22,0x41, // K
    0x7F,0x40,0x40,0x40,0x40, // L
    0x7F,0x02,0x04,0x02,0x7F, // M
    0x7F,0x04,0x08,0x10,0x7F, // N
    0x3E,0x41,0x41,0x41,0x3E, // O
    0x7F,0x09,0x09,0x09,0x06, // P
    0x3E,0x41,0x51,0x21,0x5E, // Q
    0x7F,0x09,0x19,0x29,0x46, // R
    0x46,0x49,0x49,0x49,0x31, // S
    0x01,0x01,0x7F,0x01,0x01, // T
    0x3F,0x40,0x40,0x40,0x3F, // U
    0x1F,0x20,0x40,0x20,0x1F, // V
    0x7F,0x20,0x18,0x20,0x7F, // W
    0x63,0x14,0x08,0x14,0x63, // X
    0x03,0x04,0x78,0x04,0x03, // Y
    0x61,0x51,0x49,0x45,0x43, // Z
    0x00,0x00,0x7F,0x41,0x41, // [
    0x02,0x04,0x08,0x10,0x20, // backslash
    0x41,0x41,0x7F,0x00,0x00, // ]
    0x04,0x02,0x01,0x02,0x04, // ^
    0x40,0x40,0x40,0x40,0x40, // _
    0x00,0x01,0x02,0x04,0x00, // `
    0x20,0x54,0x54,0x54,0x78, // a
    0x7F,0x48,0x44,0x44,0x38, // b
    0x38,0x44,0x44,0x44,0x20, // c
    0x38,0x44,0x44,0x48,0x7F, // d
    0x38,0x54,0x54,0x54,0x18, // e
    0x08,0x7E,0x09,0x01,0x02, // f
    0x08,0x14,0x54,0x54,0x3C, // g
    0x7F,0x08,0x04,0x04,0x78, // h
    0x00,0x44,0x7D,0x40,0x00, // i
    0x20,0x40,0x44,0x3D,0x00, // j
    0x00,0x7F,0x10,0x28,0x44, // k
    0x00,0x41,0x7F,0x40,0x00, // l
    0x7C,0x04,0x18,0x04,0x78, // m
    0x7C,0x08,0x04,0x04,0x78, // n
    0x38,0x44,0x44,0x44,0x38, // o
    0x7C,0x14,0x14,0x14,0x08, // p
    0x08,0x14,0x14,0x18,0x7C, // q
    0x7C,0x08,0x04,0x04,0x08, // r
    0x48,0x54,0x54,0x54,0x20, // s
    0x04,0x3F,0x44,0x40,0x20, // t
    0x3C,0x40,0x40,0x20,0x7C, // u
    0x1C,0x20,0x40,0x20,0x1C, // v
    0x3C,0x40,0x30,0x40,0x3C, // w
    0x44,0x28,0x10,0x28,0x44, // x
    0x0C,0x50,0x50,0x50,0x3C, // y
    0x44,0x64,0x54,0x4C,0x44, // z
    0x00,0x08,0x36,0x41,0x00, // {
    0x00,0x00,0x7F,0x00,0x00, // |
    0x00,0x41,0x36,0x08,0x00, // }
    0x08,0x08,0x2A,0x1C,0x08, // ~
};

void gfx_init(esp_lcd_panel_handle_t panel, int width, int height)
{
    lcd_panel = panel;
    lcd_width = width;
    lcd_height = height;
}

void gfx_draw_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= lcd_width || y < 0 || y >= lcd_height) return;
    esp_lcd_panel_draw_bitmap(lcd_panel, x, y, x + 1, y + 1, &color);
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x >= lcd_width || y >= lcd_height) return;
    if (x + w > lcd_width) w = lcd_width - x;
    if (y + h > lcd_height) h = lcd_height - y;
    
    uint16_t *buf = heap_caps_malloc(w * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!buf) return;
    
    for (int i = 0; i < w; i++) buf[i] = color;
    for (int row = y; row < y + h; row++) {
        esp_lcd_panel_draw_bitmap(lcd_panel, x, row, x + w, row + 1, buf);
    }
    free(buf);
}

void gfx_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    gfx_fill_rect(x, y, w, 1, color);         // top
    gfx_fill_rect(x, y + h - 1, w, 1, color); // bottom
    gfx_fill_rect(x, y, 1, h, color);         // left
    gfx_fill_rect(x + w - 1, y, 1, h, color); // right
}

void gfx_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    
    while (1) {
        gfx_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_draw_circle(int cx, int cy, int r, uint16_t color)
{
    int x = r, y = 0, err = 0;
    while (x >= y) {
        gfx_draw_pixel(cx + x, cy + y, color);
        gfx_draw_pixel(cx + y, cy + x, color);
        gfx_draw_pixel(cx - y, cy + x, color);
        gfx_draw_pixel(cx - x, cy + y, color);
        gfx_draw_pixel(cx - x, cy - y, color);
        gfx_draw_pixel(cx - y, cy - x, color);
        gfx_draw_pixel(cx + y, cy - x, color);
        gfx_draw_pixel(cx + x, cy - y, color);
        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) { x--; err += 1 - 2 * x; }
    }
}

void gfx_fill_circle(int cx, int cy, int r, uint16_t color)
{
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                gfx_draw_pixel(cx + x, cy + y, color);
            }
        }
    }
}

void gfx_draw_char_utf8(int x, int y, uint32_t codepoint, uint16_t color, uint16_t bg, int size)
{
    const uint8_t *glyph = get_glyph(codepoint);
    int pw = 5 * size;
    int ph = 7 * size;

    uint16_t *buf = heap_caps_malloc(pw * ph * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!buf) return;

    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            uint16_t pix = (bits & (1 << row)) ? color : bg;
            for (int sy = 0; sy < size; sy++)
                for (int sx = 0; sx < size; sx++)
                    buf[(row * size + sy) * pw + col * size + sx] = pix;
        }
    }

    if (x >= 0 && y >= 0 && x + pw <= lcd_width && y + ph <= lcd_height)
        esp_lcd_panel_draw_bitmap(lcd_panel, x, y, x + pw, y + ph, buf);

    free(buf);
}

void gfx_draw_char(int x, int y, char c, uint16_t color, uint16_t bg, int size)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = &font5x7[(c - 32) * 5];
    int pw = 5 * size;
    int ph = 7 * size;

    uint16_t *buf = heap_caps_malloc(pw * ph * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!buf) return;

    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            uint16_t pix = (bits & (1 << row)) ? color : bg;
            for (int sy = 0; sy < size; sy++)
                for (int sx = 0; sx < size; sx++)
                    buf[(row * size + sy) * pw + col * size + sx] = pix;
        }
    }

    if (x >= 0 && y >= 0 && x + pw <= lcd_width && y + ph <= lcd_height)
        esp_lcd_panel_draw_bitmap(lcd_panel, x, y, x + pw, y + ph, buf);

    free(buf);
}

void gfx_draw_string(int x, int y, const char *str, uint16_t color, uint16_t bg, int size)
{
    int orig_x = x;
    int len;
    
    while (*str) {
        if (*str == '\n') {
            x = orig_x;
            y += 8 * size;
            str++;
        } else {
            uint32_t codepoint = utf8_decode(str, &len);
            gfx_draw_char_utf8(x, y, codepoint, color, bg, size);
            x += 6 * size;
            str += len;
        }
    }
}

void gfx_draw_image(int x, int y, int w, int h, const uint16_t *data)
{
    esp_lcd_panel_draw_bitmap(lcd_panel, x, y, x + w, y + h, data);
}

void gfx_draw_rle_image(int x, int y, int w, int h,
                        const uint8_t *counts, const uint16_t *colors, int rle_len)
{
    int cx0 = x < 0 ? 0 : x;
    int cy0 = y < 0 ? 0 : y;
    int cx1 = (x + w > lcd_width)  ? lcd_width  : x + w;
    int cy1 = (y + h > lcd_height) ? lcd_height : y + h;
    if (cx0 >= cx1 || cy0 >= cy1) return;

    int skip_left = cx0 - x;
    int draw_w    = cx1 - cx0;
    int skip_top  = cy0 - y;

    uint16_t *line = heap_caps_malloc(w * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line) return;

    int px = 0, row = 0, ri = 0, rem = 0;
    uint16_t col = 0;

    while (row < h) {
        while (px < w) {
            if (rem == 0) {
                if (ri >= rle_len) {
                    for (; px < w; px++) line[px] = 0;
                    break;
                }
                rem = counts[ri];
                col = colors[ri++];
            }
            int can = w - px;
            if (can > rem) can = rem;
            for (int i = 0; i < can; i++) line[px + i] = col;
            px += can;
            rem -= can;
        }

        if (row >= skip_top) {
            int screen_row = cy0 + (row - skip_top);
            if (screen_row >= cy1) break;
            esp_lcd_panel_draw_bitmap(lcd_panel,
                cx0, screen_row, cx0 + draw_w, screen_row + 1,
                line + skip_left);
        }

        row++;
        px = 0;
    }

    free(line);
}

void gfx_clear(uint16_t color)
{
    gfx_fill_rect(0, 0, lcd_width, lcd_height, color);
}