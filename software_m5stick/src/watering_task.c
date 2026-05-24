// src/watering_task.c
// Логика автополива. Проверка каждую минуту.
//
// Режим 0 (по времени):
//   если прошло >= water_interval_h часов с последнего полива → полить
//
// Режим 1 (по влажности):
//   если влажность < water_threshold_pct
//   И прошло >= water_interval_h часов с последнего полива → полить
#include "app_state.h"
#include <time.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "watering"
#define CHECK_INTERVAL_MS 60000  // проверка каждую минуту

// GPIO насоса — берём из app_state.h → config.h, но здесь нужен конкретный пин.
// Пин насоса определён в main.c, но для управления из задачи вынесем его сюда.
#define PUMP_GPIO 25

static void pump_on(void)
{
    gpio_set_level(PUMP_GPIO, 1);
    STATE_LOCK();
    g_state.pump_on = true;
    STATE_UNLOCK();
}

static void pump_off(void)
{
    gpio_set_level(PUMP_GPIO, 0);
    STATE_LOCK();
    g_state.pump_on = false;
    STATE_UNLOCK();
}

void watering_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));

        time_t now;
        time(&now);
        if (now < 1000000) {
            // Время не синхронизировано (epoch слишком мало)
            ESP_LOGW(TAG, "time not synced, skip");
            continue;
        }

        STATE_LOCK();
        int  mode       = g_state.watering_mode;
        int  interval_h = g_state.water_interval_h;
        int  threshold  = g_state.water_threshold_pct;
        int  duration_s = g_state.pump_duration_s;
        int  humidity   = g_state.humidity_pct;
        int64_t last    = g_state.last_watered_s;
        STATE_UNLOCK();

        int64_t elapsed_h = (now - last) / 3600;
        bool interval_ok = (elapsed_h >= interval_h);

        bool should_water = false;
        if (mode == 0) {
            should_water = interval_ok;
        } else {
            should_water = interval_ok && (humidity < threshold);
        }

        if (!should_water) continue;

        ESP_LOGI(TAG, "watering: mode=%d, humidity=%d%%, elapsed=%lldh, duration=%ds",
                 mode, humidity, elapsed_h, duration_s);

        pump_on();
        vTaskDelay(pdMS_TO_TICKS((uint32_t)duration_s * 1000));
        pump_off();

        STATE_LOCK();
        g_state.last_watered_s = now;
        STATE_UNLOCK();

        ESP_LOGI(TAG, "watering done");
    }
}
