// src/server_task.c
// Периодически отправляет состояние на сервер и применяет полученные команды.
//
// POST SERVER_URL
// Тело: {"humidity":45,"light":true,"pump":false,"ts":1748123456}
//
// Ответ сервера (JSON, опционально):
// {
//   "watering_mode": 0,
//   "water_interval_h": 48,
//   "water_threshold_pct": 30,
//   "pump_duration_s": 10,
//   "cmd_light": "on",   // "on" / "off" / null
//   "cmd_pump":  null
// }
#include "app_state.h"
#include "config.h"
#include "server_cert.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "cJSON.h"

#define TAG        "server"
#define RESP_BUF   512
#define LIGHT_GPIO 25
#define PUMP_GPIO  26

static char resp_buf[RESP_BUF];
static int  resp_len;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int copy = evt->data_len;
        if (resp_len + copy >= RESP_BUF) copy = RESP_BUF - resp_len - 1;
        if (copy > 0) {
            memcpy(resp_buf + resp_len, evt->data, copy);
            resp_len += copy;
        }
    }
    return ESP_OK;
}

static void apply_response(const char *json)
{
    cJSON *root = cJSON_ParseWithLength(json, resp_len);
    if (!root) return;

    // Разбор JSON без лока — cJSON работает только со своим деревом.
    cJSON *v;
    cJSON *cmd_light = cJSON_GetObjectItem(root, "cmd_light");
    cJSON *cmd_pump  = cJSON_GetObjectItem(root, "cmd_pump");

    bool have_light = cmd_light && cJSON_IsString(cmd_light);
    bool have_pump  = cmd_pump  && cJSON_IsString(cmd_pump);
    bool light_on   = have_light && strcmp(cmd_light->valuestring, "on") == 0;
    bool pump_on    = have_pump  && strcmp(cmd_pump->valuestring,  "on") == 0;

    // Все изменения g_state — под одним локом, без I/O внутри.
    STATE_LOCK();
    if ((v = cJSON_GetObjectItem(root, "watering_mode")) && cJSON_IsNumber(v))
        g_state.watering_mode = v->valueint;
    if ((v = cJSON_GetObjectItem(root, "water_interval_h")) && cJSON_IsNumber(v))
        g_state.water_interval_h = v->valueint;
    if ((v = cJSON_GetObjectItem(root, "water_threshold_pct")) && cJSON_IsNumber(v))
        g_state.water_threshold_pct = v->valueint;
    if ((v = cJSON_GetObjectItem(root, "pump_duration_s")) && cJSON_IsNumber(v))
        g_state.pump_duration_s = v->valueint;
    if (have_light) g_state.light_on = light_on;
    if (have_pump) {
        g_state.pump_on = pump_on;
        g_state.pump_stop_at = pump_on
            ? (int64_t)time(NULL) + g_state.pump_duration_s
            : 0;
    }
    STATE_UNLOCK();

    // GPIO и логи — после освобождения мьютекса.
    if (have_light) {
        gpio_set_level(LIGHT_GPIO, light_on ? 1 : 0);
        ESP_LOGI(TAG, "cmd_light: %s", light_on ? "on" : "off");
    }
    if (have_pump) {
        gpio_set_level(PUMP_GPIO, pump_on ? 1 : 0);
        ESP_LOGI(TAG, "cmd_pump: %s", pump_on ? "on" : "off");
    }

    cJSON_Delete(root);
}

void server_task(void *arg)
{
    char body[128];

    esp_http_client_config_t cfg = {
        .url                = SERVER_URL,
        .method             = HTTP_METHOD_POST,
        .event_handler      = http_event_handler,
        .timeout_ms         = 5000,
        .cert_pem           = SERVER_CERT_PEM,
        .keep_alive_enable  = true,
    };

    // Хендл создаётся один раз — TLS-сессия переиспользуется между запросами.
    // При обрыве соединения esp_http_client_perform переподключается автоматически.
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    uint32_t poll_ms = (uint32_t)SERVER_POLL_S * 1000;
    uint32_t log_counter = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(poll_ms));

        time_t now;
        time(&now);

        STATE_LOCK();
        int  hum   = g_state.humidity_pct;
        bool light = g_state.light_on;
        bool pump  = g_state.pump_on;
        STATE_UNLOCK();

        snprintf(body, sizeof(body),
                 "{\"humidity\":%d,\"light\":%s,\"pump\":%s,\"ts\":%lld}",
                 hum, light ? "true" : "false", pump ? "true" : "false",
                 (long long)now);

        resp_len = 0;
        memset(resp_buf, 0, sizeof(resp_buf));
        esp_http_client_set_post_field(client, body, strlen(body));

        TickType_t t0 = xTaskGetTickCount();
        esp_err_t err = esp_http_client_perform(client);
        uint32_t dt_ms = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;

        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            if (status == 200 && resp_len > 0) {
                resp_buf[resp_len] = '\0';
                apply_response(resp_buf);
            }
            // Сигналим в лог только если запрос подозрительно долгий — обычно молчим.
            if (dt_ms > 1000) {
                ESP_LOGW(TAG, "POST slow: %lu ms status=%d", (unsigned long)dt_ms, status);
            }
        } else {
            ESP_LOGW(TAG, "POST failed in %lu ms: %s", (unsigned long)dt_ms, esp_err_to_name(err));
            // Принудительно закрыть соединение — при следующем вызове
            // perform() переподключится с новым TLS-хендшейком.
            esp_http_client_close(client);
        }

        // Раз в 30 секунд выводим статистику ресурсов
        log_counter += poll_ms;
        if (log_counter >= 30000) {
            log_counter = 0;
            ESP_LOGI(TAG, "heap free=%lu min=%lu | DMA=%lu | stk=%lu words",
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)esp_get_minimum_free_heap_size(),
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA),
                     (unsigned long)uxTaskGetStackHighWaterMark(NULL));
        }
    }
}
