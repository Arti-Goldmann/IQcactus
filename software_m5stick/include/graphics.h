// graphics.h
#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

// Цвета (уже со SWAP)
#define SWAP16(x) (((x) >> 8) | (((x) & 0xFF) << 8))
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   SWAP16(0xFFFF)
#define COLOR_RED     SWAP16(0xF800)
#define COLOR_GREEN   SWAP16(0x07E0)
#define COLOR_BLUE    SWAP16(0x001F)
#define COLOR_YELLOW  SWAP16(0xFFE0)
#define COLOR_CYAN    SWAP16(0x07FF)
#define COLOR_MAGENTA SWAP16(0xF81F)
#define COLOR_ORANGE  SWAP16(0xFC00)

// Полная инициализация: SPI bus + ST7789 + внутренние буферы рендеринга.
// Реализация — polling SPI без зависимости от DMA completion-IRQ.
void gfx_lcd_init(int mosi_pin, int sclk_pin, int cs_pin, int dc_pin,
                  int rst_pin, int bl_pin,
                  int width, int height, int gap_x, int gap_y);

void gfx_draw_pixel(int x, int y, uint16_t color);
void gfx_draw_line(int x0, int y0, int x1, int y1, uint16_t color);
void gfx_draw_rect(int x, int y, int w, int h, uint16_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);
void gfx_draw_circle(int cx, int cy, int r, uint16_t color);
void gfx_fill_circle(int cx, int cy, int r, uint16_t color);
void gfx_draw_char(int x, int y, char c, uint16_t color, uint16_t bg, int size);
void gfx_draw_string(int x, int y, const char *str, uint16_t color, uint16_t bg, int size);
void gfx_draw_image(int x, int y, int w, int h, const uint16_t *data);
void gfx_draw_rle_image(int x, int y, int w, int h,
                        const uint8_t *counts, const uint16_t *colors, int rle_len);
void gfx_clear(uint16_t color);

// Диагностика: счётчики вызовов draw_bitmap и последняя ошибка от драйвера.
void gfx_get_stats(uint32_t *calls, uint32_t *errs, int *last_err);

// Диагностика «in-flight» SPI: enter инкрементится перед polling-transmit,
// exit — после. Если enter > exit, задача застряла внутри SPI-вызова
// (с polling это уже не должно случаться).
void gfx_get_bmp_inflight(uint32_t *enter, uint32_t *exit);

// Диагностика: текущее состояние декодера RLE (для отлова зависаний внутри
// gfx_draw_rle_image). enter/exit — счётчики входов и нормальных выходов.
void gfx_get_rle_state(uint32_t *enter, uint32_t *exit,
                       int *row, int *px, int *ri,
                       int *w, int *h, int *len);

#endif
