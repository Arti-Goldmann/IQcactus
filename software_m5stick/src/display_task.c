// src/display_task.c
#include "app_state.h"
#include "graphics.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#define TAG_DISP "display"

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
//   x=121,y=4  → капля 14×14px   (правый верх)
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
            gfx_draw_rle_image(121, 4, RAINDROP_WIDTH, RAINDROP_HEIGHT,
                               raindrop_night_rle_counts, raindrop_night_rle_colors,
                               RAINDROP_NIGHT_RLE_LEN);
        } else {
            gfx_draw_rle_image(121, 4, RAINDROP_WIDTH, RAINDROP_HEIGHT,
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

void display_task(void *arg)
{
    int frame = 0;
    int stk_log = 0;

    while (1) {
        bool light, pump;
        STATE_LOCK();
        light = g_state.light_on;
        pump  = g_state.pump_on;
        STATE_UNLOCK();

        bool night = !light && !pump;

        if (pump) {
            draw_sprite(&raindrops[frame % 2]);
        } else if (light) {
            draw_sprite(&daytime[frame % 4]);
        } else {
            draw_sprite(&nighttime[frame % 2]);
        }

        draw_hud(night);
        frame++;

        if (++stk_log >= 30) {
            stk_log = 0;
            ESP_LOGI(TAG_DISP, "stk=%lu words",
                     (unsigned long)uxTaskGetStackHighWaterMark(NULL));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
