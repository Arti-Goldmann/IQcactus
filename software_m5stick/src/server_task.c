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
#include "driver/gpio.h"
#include "cJSON.h"

#define TAG        "server"
#define RESP_BUF   512
#define LIGHT_GPIO 26
#define PUMP_GPIO  25

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

    STATE_LOCK();

    cJSON *v;
    if ((v = cJSON_GetObjectItem(root, "watering_mode")) && cJSON_IsNumber(v))
        g_state.watering_mode = v->valueint;
    if ((v = cJSON_GetObjectItem(root, "water_interval_h")) && cJSON_IsNumber(v))
        g_state.water_interval_h = v->valueint;
    if ((v = cJSON_GetObjectItem(root, "water_threshold_pct")) && cJSON_IsNumber(v))
        g_state.water_threshold_pct = v->valueint;
    if ((v = cJSON_GetObjectItem(root, "pump_duration_s")) && cJSON_IsNumber(v))
        g_state.pump_duration_s = v->valueint;

    cJSON *cmd_light = cJSON_GetObjectItem(root, "cmd_light");
    if (cmd_light && cJSON_IsString(cmd_light)) {
        bool on = strcmp(cmd_light->valuestring, "on") == 0;
        g_state.light_on = on;
        STATE_UNLOCK();
        gpio_set_level(LIGHT_GPIO, on ? 1 : 0);
        ESP_LOGI(TAG, "cmd_light: %s", on ? "on" : "off");
        STATE_LOCK();
    }

    cJSON *cmd_pump = cJSON_GetObjectItem(root, "cmd_pump");
    if (cmd_pump && cJSON_IsString(cmd_pump)) {
        bool on = strcmp(cmd_pump->valuestring, "on") == 0;
        g_state.pump_on = on;
        STATE_UNLOCK();
        gpio_set_level(PUMP_GPIO, on ? 1 : 0);
        ESP_LOGI(TAG, "cmd_pump: %s", on ? "on" : "off");
        STATE_LOCK();
    }

    STATE_UNLOCK();
    cJSON_Delete(root);
}

void server_task(void *arg)
{
    char body[128];

    esp_http_client_config_t cfg = {
        .url           = SERVER_URL,
        .method        = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms    = 10000,
        .cert_pem      = SERVER_CERT_PEM,
    };

    while (1) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)SERVER_POLL_S * 1000));

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

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "POST %d, body: %s", status, body);
            if (status == 200 && resp_len > 0) {
                resp_buf[resp_len] = '\0';
                apply_response(resp_buf);
            }
        } else {
            ESP_LOGW(TAG, "POST failed: %s", esp_err_to_name(err));
        }

        esp_http_client_cleanup(client);
    }
}
