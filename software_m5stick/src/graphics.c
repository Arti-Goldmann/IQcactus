// graphics.c
#include "graphics.h"
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "font_cyrillic.h"

#define TAG_GFX "gfx"

// ---- Низкоуровневый ST7789 на polling SPI ----------------------------------
// Отказались от esp_lcd_panel_io_spi: его DMA-based реализация теряла
// completion-IRQ и зависала навсегда. spi_device_polling_transmit использует
// DMA для передачи, но завершение проверяет в нашем CPU-цикле — без зависимости
// от внешнего прерывания, поэтому потерять «завершение» физически нельзя.

static spi_device_handle_t g_spi;
static int g_dc_pin;
static int g_gap_x;
static int g_gap_y;

static int lcd_width;
static int lcd_height;

// Forward declarations: ST7789-хелперы определены ниже, но используются
// в gfx_draw_bitmap_checked, который объявлен выше них.
static void st7789_send_cmd(uint8_t cmd);
static void st7789_send_data(const void *data, size_t len);
static void st7789_set_window(int x0, int y0, int x1, int y1);

static uint16_t *g_line_buf[2];
static uint16_t *g_char_buf[2];
static unsigned  g_line_idx = 0;
static unsigned  g_char_idx = 0;

// Счётчики для диагностики «зависания» дисплея.
// Читаются из display_task через gfx_get_stats().
// g_bitmap_enter инкрементится непосредственно ПЕРЕД esp_lcd_panel_draw_bitmap,
// g_bitmap_exit — сразу ПОСЛЕ. Разность enter-exit показывает, сидим ли мы внутри
// драйвера SPI/LCD в момент опроса.
static volatile uint32_t g_bitmap_calls = 0;
static volatile uint32_t g_bitmap_enter = 0;
static volatile uint32_t g_bitmap_exit  = 0;
static volatile uint32_t g_bitmap_errs  = 0;
static volatile int      g_last_err     = 0;

// Трекинг прогресса внутри gfx_draw_rle_image — если зависнем там,
// watchdog покажет, на какой строке/позиции/RLE-индексе застряли.
static volatile uint32_t g_rle_enter   = 0;  // счётчик входов в функцию
static volatile uint32_t g_rle_exit    = 0;  // счётчик нормальных выходов
static volatile int      g_rle_row     = 0;
static volatile int      g_rle_px      = 0;
static volatile int      g_rle_ri      = 0;
static volatile int      g_rle_w       = 0;
static volatile int      g_rle_h       = 0;
static volatile int      g_rle_len     = 0;

static __attribute__((noinline))
void gfx_draw_bitmap_checked(int x0, int y0, int x1, int y1, const void *data)
{
    g_bitmap_calls++;
    g_bitmap_enter++;

    int w = x1 - x0;
    int h = y1 - y0;
    if (w <= 0 || h <= 0) { g_bitmap_exit++; return; }

    st7789_set_window(x0, y0, x1, y1);

    // Шлём пиксели в режиме polling: ждём завершения каждой транзакции в нашем
    // цикле, IRQ-семафоры не используются.
    gpio_set_level(g_dc_pin, 1);
    size_t bytes = (size_t)w * h * 2;
    spi_transaction_t t = {
        .length    = bytes * 8,
        .tx_buffer = data,
    };
    esp_err_t err = spi_device_polling_transmit(g_spi, &t);

    g_bitmap_exit++;
    if (err != ESP_OK) {
        g_bitmap_errs++;
        g_last_err = err;
        ESP_LOGE(TAG_GFX, "draw_bitmap fail: %s (x=%d..%d y=%d..%d)",
                 esp_err_to_name(err), x0, x1, y0, y1);
    }
}

void gfx_get_stats(uint32_t *calls, uint32_t *errs, int *last_err)
{
    if (calls)    *calls    = g_bitmap_calls;
    if (errs)     *errs     = g_bitmap_errs;
    if (last_err) *last_err = g_last_err;
}

void gfx_get_bmp_inflight(uint32_t *enter, uint32_t *exit)
{
    if (enter) *enter = g_bitmap_enter;
    if (exit)  *exit  = g_bitmap_exit;
}

void gfx_get_rle_state(uint32_t *enter, uint32_t *exit,
                       int *row, int *px, int *ri,
                       int *w, int *h, int *len)
{
    if (enter) *enter = g_rle_enter;
    if (exit)  *exit  = g_rle_exit;
    if (row)   *row   = g_rle_row;
    if (px)    *px    = g_rle_px;
    if (ri)    *ri    = g_rle_ri;
    if (w)     *w     = g_rle_w;
    if (h)     *h     = g_rle_h;
    if (len)   *len   = g_rle_len;
}

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

static void st7789_send_cmd(uint8_t cmd)
{
    gpio_set_level(g_dc_pin, 0);
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(g_spi, &t);
}

static void st7789_send_data(const void *data, size_t len)
{
    if (len == 0) return;
    gpio_set_level(g_dc_pin, 1);
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(g_spi, &t);
}

static void st7789_set_window(int x0, int y0, int x1, int y1)
{
    // x1/y1 — exclusive: контроллеру передаём (x1-1).
    int xs = x0 + g_gap_x;
    int xe = (x1 - 1) + g_gap_x;
    int ys = y0 + g_gap_y;
    int ye = (y1 - 1) + g_gap_y;

    uint8_t buf[4];

    st7789_send_cmd(0x2A);                  // CASET
    buf[0] = xs >> 8; buf[1] = xs & 0xFF;
    buf[2] = xe >> 8; buf[3] = xe & 0xFF;
    st7789_send_data(buf, 4);

    st7789_send_cmd(0x2B);                  // RASET
    buf[0] = ys >> 8; buf[1] = ys & 0xFF;
    buf[2] = ye >> 8; buf[3] = ye & 0xFF;
    st7789_send_data(buf, 4);

    st7789_send_cmd(0x2C);                  // RAMWR — далее идут пиксели
}

void gfx_lcd_init(int mosi_pin, int sclk_pin, int cs_pin, int dc_pin,
                  int rst_pin, int bl_pin,
                  int width, int height, int gap_x, int gap_y)
{
    lcd_width = width;
    lcd_height = height;
    g_dc_pin = dc_pin;
    g_gap_x  = gap_x;
    g_gap_y  = gap_y;

    // Подсветка и управляющие GPIO
    gpio_reset_pin(bl_pin);
    gpio_set_direction(bl_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(bl_pin, 1);

    gpio_reset_pin(dc_pin);
    gpio_set_direction(dc_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(dc_pin, 1);

    gpio_reset_pin(rst_pin);
    gpio_set_direction(rst_pin, GPIO_MODE_OUTPUT);

    // SPI шина — DMA включён, потому что polling_transmit использует его
    // для самой передачи (но завершение всё равно опрашиваем сами).
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = mosi_pin,
        .miso_io_num     = -1,
        .sclk_io_num     = sclk_pin,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = width * height * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = cs_pin,
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &g_spi));

    // Аппаратный reset ST7789
    gpio_set_level(rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Init sequence
    st7789_send_cmd(0x01);                  // SWRESET
    vTaskDelay(pdMS_TO_TICKS(150));

    st7789_send_cmd(0x11);                  // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(120));

    st7789_send_cmd(0x3A);                  // COLMOD
    uint8_t colmod = 0x55;                  // 16-bit/pixel RGB565
    st7789_send_data(&colmod, 1);

    st7789_send_cmd(0x36);                  // MADCTL
    uint8_t madctl = 0xC0;                  // MX|MY = mirror(true,true), RGB
    st7789_send_data(&madctl, 1);

    st7789_send_cmd(0x21);                  // INVON — инверсия цветов
    st7789_send_cmd(0x13);                  // NORON — нормальный режим
    st7789_send_cmd(0x29);                  // DISPON

    // Буферы построчного рендеринга
    for (int i = 0; i < 2; i++) {
        g_line_buf[i] = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_DMA);
        g_char_buf[i] = heap_caps_malloc(5 * 4 * 7 * 4 * sizeof(uint16_t), MALLOC_CAP_DMA);
    }

    ESP_LOGI(TAG_GFX, "ST7789 polling SPI init done (%dx%d)", width, height);
}

void gfx_draw_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= lcd_width || y < 0 || y >= lcd_height) return;
    gfx_draw_bitmap_checked(x, y, x + 1, y + 1, &color);
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x >= lcd_width || y >= lcd_height) return;
    if (x + w > lcd_width)  w = lcd_width - x;
    if (y + h > lcd_height) h = lcd_height - y;

    for (int row = y; row < y + h; row++) {
        uint16_t *buf = g_line_buf[g_line_idx & 1];
        g_line_idx++;
        for (int i = 0; i < w; i++) buf[i] = color;
        gfx_draw_bitmap_checked(x, row, x + w, row + 1, buf);
    }
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

    uint16_t *buf = g_char_buf[g_char_idx & 1];
    g_char_idx++;

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
        gfx_draw_bitmap_checked(x, y, x + pw, y + ph, buf);
}

void gfx_draw_char(int x, int y, char c, uint16_t color, uint16_t bg, int size)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = &font5x7[(c - 32) * 5];
    int pw = 5 * size;
    int ph = 7 * size;

    uint16_t *buf = g_char_buf[g_char_idx & 1];
    g_char_idx++;

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
        gfx_draw_bitmap_checked(x, y, x + pw, y + ph, buf);
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
    gfx_draw_bitmap_checked(x, y, x + w, y + h, data);
}

void gfx_draw_rle_image(int x, int y, int w, int h,
                        const uint8_t *counts, const uint16_t *colors, int rle_len)
{
    g_rle_enter++;
    g_rle_w = w; g_rle_h = h; g_rle_len = rle_len;
    g_rle_row = 0; g_rle_px = 0; g_rle_ri = 0;

    int cx0 = x < 0 ? 0 : x;
    int cy0 = y < 0 ? 0 : y;
    int cx1 = (x + w > lcd_width)  ? lcd_width  : x + w;
    int cy1 = (y + h > lcd_height) ? lcd_height : y + h;
    if (cx0 >= cx1 || cy0 >= cy1) { g_rle_exit++; return; }

    int skip_left = cx0 - x;
    int draw_w    = cx1 - cx0;
    int skip_top  = cy0 - y;

    int px = 0, row = 0, ri = 0, rem = 0;
    uint16_t col = 0;

    while (row < h) {
        // Берём буфер и сразу сдвигаем индекс — следующая итерация будет
        // заполнять другой буфер, пока DMA дочитывает текущий.
        uint16_t *line = g_line_buf[g_line_idx & 1];
        g_line_idx++;

        g_rle_row = row;

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
            g_rle_px = px;
            g_rle_ri = ri;
        }

        if (row >= skip_top) {
            int screen_row = cy0 + (row - skip_top);
            if (screen_row >= cy1) break;
            gfx_draw_bitmap_checked(cx0, screen_row, cx0 + draw_w, screen_row + 1,
                                    line + skip_left);
        }

        row++;
        px = 0;
    }

    g_rle_exit++;
}

void gfx_clear(uint16_t color)
{
    gfx_fill_rect(0, 0, lcd_width, lcd_height, color);
}