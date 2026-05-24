#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "config.h"

typedef struct {
    int  humidity_pct;          // влажность 0-100%
    bool light_on;              // состояние света (G26)
    bool pump_on;               // состояние насоса (G25)

    // Уставки полива (могут обновляться сервером)
    int  watering_mode;         // 0=по времени, 1=по влажности
    int  water_interval_h;      // минимальный интервал между поливами, часы
    int  water_threshold_pct;   // порог влажности для режима 1
    int  pump_duration_s;       // длительность работы насоса, сек
    int64_t last_watered_s;     // unix timestamp последнего полива
} AppState;

extern AppState g_state;
extern SemaphoreHandle_t g_state_mutex;

// Захватить мьютекс перед чтением/записью g_state (таймаут 100 мс)
#define STATE_LOCK()   xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100))
#define STATE_UNLOCK() xSemaphoreGive(g_state_mutex)
