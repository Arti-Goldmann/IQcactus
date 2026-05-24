// src/display_task.c
// Задача: обновление дисплея каждую секунду.
// Логика анимации:
//   насос включён → raindrops1/2 (независимо от света)
//   свет включён  → daytime1/2/3
//   иначе         → night1/2
#include "app_state.h"
#include "graphics.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Заголовки спрайтов (RLE, 136x136 px)
#include "sprites/sprite_kaktus_daytime1.h"
#include "sprites/sprite_kaktus_daytime2.h"
#include "sprites/sprite_kaktus_daytime3.h"
#include "sprites/sprite_kaktus_night1.h"
#include "sprites/sprite_kaktus_night2.h"
#include "sprites/sprite_kaktus_raindrops1.h"
#include "sprites/sprite_kaktus_raindrops2.h"

// Спрайт располагается в центре дисплея (135x240)
// 136x136 → x=0 (1 пиксель обрезается справа), y=52
#define SPRITE_X 0
#define SPRITE_Y 52

typedef struct {
    const uint8_t  *counts;
    const uint16_t *colors;
    int             rle_len;
    int             w;
    int             h;
} Sprite;

static const Sprite daytime[3] = {
    { sprite_kaktus_daytime1_rle_counts, sprite_kaktus_daytime1_rle_colors,
      SPRITE_KAKTUS_DAYTIME1_RLE_LEN, SPRITE_KAKTUS_DAYTIME1_WIDTH, SPRITE_KAKTUS_DAYTIME1_HEIGHT },
    { sprite_kaktus_daytime2_rle_counts, sprite_kaktus_daytime2_rle_colors,
      SPRITE_KAKTUS_DAYTIME2_RLE_LEN, SPRITE_KAKTUS_DAYTIME2_WIDTH, SPRITE_KAKTUS_DAYTIME2_HEIGHT },
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

static void draw(const Sprite *s)
{
    gfx_draw_rle_image(SPRITE_X, SPRITE_Y, s->w, s->h,
                       s->counts, s->colors, s->rle_len);
}

void display_task(void *arg)
{
    int frame = 0;

    while (1) {
        bool light, pump;
        STATE_LOCK();
        light = g_state.light_on;
        pump  = g_state.pump_on;
        STATE_UNLOCK();

        if (pump) {
            draw(&raindrops[frame % 2]);
        } else if (light) {
            draw(&daytime[frame % 3]);
        } else {
            draw(&nighttime[frame % 2]);
        }

        frame++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
