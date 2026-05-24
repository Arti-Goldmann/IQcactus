// src/display_task.c
#include "app_state.h"
#include "graphics.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#define TAG_DISP "display"

// Маркер последней пройденной точки внутри цикла display_task.
// Если задача зависнет, отдельный watchdog-таск увидит, что счётчик кадров
// не растёт, и распечатает последний stage — это покажет, где именно встали.
static volatile uint32_t g_disp_frame = 0;
static volatile uint8_t  g_disp_stage = 0;
static TaskHandle_t      g_disp_handle = NULL;
#define STAGE(n)  do { g_disp_stage = (n); } while (0)

#include "sprites/sprite_kaktus_daytime1.h"
#include "sprites/sprite_kaktus_daytime2.h"
#include "sprites/sprite_kaktus_daytime3.h"
#include "sprites/sprite_kaktus_night1.h"
#include "sprites/sprite_kaktus_night2.h"
#include "sprites/sprite_kaktus_raindrops1.h"
#include "sprites/sprite_kaktus_raindrops2.h"
#include "sprites/sprite_drop.h"
#include "sprites/sprite_drop_night.h"

// Верхние 38px — зона HUD, спрайт начинается ниже
#define HUD_HEIGHT 38
#define SPRITE_X   0
#define SPRITE_Y   HUD_HEIGHT

typedef struct {
    const uint8_t  *counts;
    const uint16_t *colors;
    int             rle_len;
    int             w;
    int             h;
} Sprite;

// Последовательность: 1 → 2 → 1 → 3 → (повтор)
static const Sprite daytime[4] = {
    { sprite_kaktus_daytime1_rle_counts, sprite_kaktus_daytime1_rle_colors,
      SPRITE_KAKTUS_DAYTIME1_RLE_LEN, SPRITE_KAKTUS_DAYTIME1_WIDTH, SPRITE_KAKTUS_DAYTIME1_HEIGHT },
    { sprite_kaktus_daytime2_rle_counts, sprite_kaktus_daytime2_rle_colors,
      SPRITE_KAKTUS_DAYTIME2_RLE_LEN, SPRITE_KAKTUS_DAYTIME2_WIDTH, SPRITE_KAKTUS_DAYTIME2_HEIGHT },
    { sprite_kaktus_daytime1_rle_counts, sprite_kaktus_daytime1_rle_colors,
      SPRITE_KAKTUS_DAYTIME1_RLE_LEN, SPRITE_KAKTUS_DAYTIME1_WIDTH, SPRITE_KAKTUS_DAYTIME1_HEIGHT },
    { sprite_kaktus_daytime3_rle_counts, sprite_kaktus_daytime3_rle_colors,
      SPRITE_KAKTUS_DAYTIME3_RLE_LEN, SPRITE_KAKTUS_DAYTIME3_WIDTH, SPRITE_KAKTUS_DAYTIME3_HEIGHT },
};

static const Sprite nighttime[2] = {
    { sprite_kaktus_night1_rle_counts, sprite_kaktus_night1_rle_colors,
      SPRITE_KAKTUS_NIGHT1_RLE_LEN, SPRITE_KAKTUS_NIGHT1_WIDTH, SPRITE_KAKTUS_NIGHT1_HEIGHT },
    { sprite_kaktus_night2_rle_counts, sprite_kaktus_night2_rle_colors,
      SPRITE_KAKTUS_NIGHT2_RLE_LEN, SPRITE_KAKTUS_NIGHT2_WIDTH, SPRITE_KAKTUS_NIGHT2_HEIGHT },
};

static const Sprite raindrops[2] = {
    { sprite_kaktus_raindrops1_rle_counts, sprite_kaktus_raindrops1_rle_colors,
      SPRITE_KAKTUS_RAINDROPS1_RLE_LEN, SPRITE_KAKTUS_RAINDROPS1_WIDTH, SPRITE_KAKTUS_RAINDROPS1_HEIGHT },
    { sprite_kaktus_raindrops2_rle_counts, sprite_kaktus_raindrops2_rle_colors,
      SPRITE_KAKTUS_RAINDROPS2_RLE_LEN, SPRITE_KAKTUS_RAINDROPS2_WIDTH, SPRITE_KAKTUS_RAINDROPS2_HEIGHT },
};

static void draw_sprite(const Sprite *s)
{
    gfx_draw_rle_image(SPRITE_X, SPRITE_Y, s->w, s->h,
                       s->counts, s->colors, s->rle_len);
}

// HUD: время слева, капля + влажность справа.
// Вызывается только при изменении данных или смене режима.
//
// Layout (scale=2, каждый символ 12×14px):
//   x=4,  y=4  → "12:34"         (время)
//   x=110,y=4  → капля 14×14px   (правый верх)
//   x=83, y=20 → " 45%"          (под каплей, правый край = 83+48=131)
static void draw_hud(bool night)
{
    static char prev_time[6] = {0};
    static char prev_hum[5]  = {0};
    static bool prev_night   = false;
    static bool hud_init     = false;

    uint16_t fg = night ? COLOR_WHITE : COLOR_BLACK;
    uint16_t bg = night ? COLOR_BLACK : COLOR_WHITE;

    bool mode_changed = !hud_init || (night != prev_night);

    // При смене режима (день/ночь) заливаем полосу HUD фоновым цветом
    if (mode_changed) {
        gfx_fill_rect(0, 0, 135, HUD_HEIGHT, bg);
        prev_time[0] = 0;
        prev_hum[0]  = 0;
    }

    // Время — обновляем только при изменении (раз в минуту)
    time_t now;
    time(&now);
    char time_str[6] = "--:--";
    if (now > 100000LL) {
        struct tm *t = localtime(&now);
        snprintf(time_str, sizeof(time_str), "%02d:%02d", t->tm_hour, t->tm_min);
    }
    if (strcmp(time_str, prev_time) != 0) {
        gfx_draw_string(4, 4, time_str, fg, bg, 2);
        memcpy(prev_time, time_str, sizeof(time_str));
    }

    // Иконка капли — только при смене режима
    if (mode_changed) {
        if (night) {
            gfx_draw_rle_image(110, 4, RAINDROP_WIDTH, RAINDROP_HEIGHT,
                               raindrop_night_rle_counts, raindrop_night_rle_colors,
                               RAINDROP_NIGHT_RLE_LEN);
        } else {
            gfx_draw_rle_image(110, 4, RAINDROP_WIDTH, RAINDROP_HEIGHT,
                               raindrop_rle_counts, raindrop_rle_colors,
                               RAINDROP_RLE_LEN);
        }
    }

    // Влажность — обновляем только при изменении значения
    STATE_LOCK();
    int hum = g_state.humidity_pct;
    STATE_UNLOCK();
    char hum_str[5];
    snprintf(hum_str, sizeof(hum_str), "%3d%%", hum);
    if (strcmp(hum_str, prev_hum) != 0) {
        gfx_draw_string(83, 20, hum_str, fg, bg, 2);
        memcpy(prev_hum, hum_str, sizeof(hum_str));
    }

    prev_night = night;
    hud_init   = true;
}

// Фоновый watchdog: каждые 5 секунд проверяет, что display_task продвинулся
// хотя бы на 1 кадр. Если нет — печатает stage, состояние декодера RLE,
// счётчики gfx, кучу и stack water mark самой display_task.
// Если задача застряла внутри SPI на 3 тика watchdog подряд (~15 с),
// перезагружаемся — это последний рубеж против вечного зависания LCD-драйвера.
#define DISPLAY_FREEZE_RESTART_TICKS 3

static void display_watchdog_task(void *arg)
{
    uint32_t prev_frame = 0;
    int      freeze_streak = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        uint32_t cur = g_disp_frame;
        uint32_t calls = 0, errs = 0;
        int last_err = 0;
        gfx_get_stats(&calls, &errs, &last_err);

        uint32_t bmp_in = 0, bmp_out = 0;
        gfx_get_bmp_inflight(&bmp_in, &bmp_out);
        bool inside_spi = (bmp_in != bmp_out);

        uint32_t rle_in = 0, rle_out = 0;
        int rle_row = 0, rle_px = 0, rle_ri = 0, rle_w = 0, rle_h = 0, rle_len = 0;
        gfx_get_rle_state(&rle_in, &rle_out, &rle_row, &rle_px, &rle_ri,
                          &rle_w, &rle_h, &rle_len);

        unsigned long stk = g_disp_handle
            ? (unsigned long)uxTaskGetStackHighWaterMark(g_disp_handle) : 0;

        if (cur == prev_frame) {
            freeze_streak++;
            ESP_LOGE(TAG_DISP,
                     "FREEZE[%d]: frame=%lu stage=%u stk=%lu | SPI%s in=%lu out=%lu"
                     " | bmp calls=%lu errs=%lu last=%d"
                     " | RLE in=%lu out=%lu row=%d/%d px=%d/%d ri=%d/%d"
                     " | heap free=%lu min=%lu DMA=%lu",
                     freeze_streak,
                     (unsigned long)cur, (unsigned)g_disp_stage, stk,
                     inside_spi ? "(INSIDE!)" : "",
                     (unsigned long)bmp_in, (unsigned long)bmp_out,
                     (unsigned long)calls, (unsigned long)errs, last_err,
                     (unsigned long)rle_in, (unsigned long)rle_out,
                     rle_row, rle_h, rle_px, rle_w, rle_ri, rle_len,
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)esp_get_minimum_free_heap_size(),
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA));

            if (freeze_streak >= DISPLAY_FREEZE_RESTART_TICKS) {
                ESP_LOGE(TAG_DISP, "FREEZE persistent — restarting chip");
                // Дать строке успеть улететь в UART
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
        } else {
            freeze_streak = 0;
            ESP_LOGI(TAG_DISP,
                     "alive: frame=%lu (+%lu) stage=%u stk=%lu | SPI in=%lu out=%lu"
                     " | bmp calls=%lu errs=%lu | RLE in=%lu out=%lu"
                     " | heap free=%lu DMA=%lu",
                     (unsigned long)cur, (unsigned long)(cur - prev_frame),
                     (unsigned)g_disp_stage, stk,
                     (unsigned long)bmp_in, (unsigned long)bmp_out,
                     (unsigned long)calls, (unsigned long)errs,
                     (unsigned long)rle_in, (unsigned long)rle_out,
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA));
        }
        prev_frame = cur;
    }
}

void display_task(void *arg)
{
    int frame = 0;

    g_disp_handle = xTaskGetCurrentTaskHandle();

    // Поднимаем watchdog на низком приоритете и на ПРОТИВОПОЛОЖНОМ ядре
    // (display_task пинуется на core 1), чтобы он всегда мог отчитаться,
    // даже если ядро 1 целиком встало в SPI/DMA.
    xTaskCreatePinnedToCore(display_watchdog_task, "disp_wdg", 3072, NULL, 2, NULL, 0);

    while (1) {
        STAGE(1);
        bool light, pump;
        STATE_LOCK();
        light = g_state.light_on;
        pump  = g_state.pump_on;
        STATE_UNLOCK();

        bool night = !light && !pump;

        STAGE(2);
        if (pump) {
            draw_sprite(&raindrops[frame % 2]);
        } else if (light) {
            draw_sprite(&daytime[frame % 4]);
        } else {
            draw_sprite(&nighttime[frame % 2]);
        }

        STAGE(3);
        draw_hud(night);

        STAGE(4);
        frame++;
        g_disp_frame = frame;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
