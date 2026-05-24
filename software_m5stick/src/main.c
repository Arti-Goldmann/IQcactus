// src/main.c
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "graphics.h"
#include "app_state.h"
#include "wifi.h"

#define TAG "main"

// Дисплей
#define LCD_MOSI    15
#define LCD_CLK     13
#define LCD_CS       5
#define LCD_DC      14
#define LCD_RST     12
#define LCD_BL      27
#define LCD_WIDTH  135
#define LCD_HEIGHT 240

// Питание
#define HOLD_PIN     4

// Кнопки (active LOW)
#define BTN_A_PIN   37   // BTN_A → свет (G26)
#define BTN_B_PIN   39   // BTN_B → насос (G25)

// Выходы
#define OUT_G26     26   // свет
#define OUT_G25     25   // насос

// Датчик влажности (ADC1_CH4 = GPIO32)
#define MOISTURE_ADC_CHANNEL  ADC_CHANNEL_4
#define MOISTURE_DRY_RAW      3270
#define MOISTURE_WET_RAW       820
#define MOISTURE_AVG_N           8

#define DEBOUNCE_MS 50

// Глобальное состояние и мьютекс
AppState g_state;
SemaphoreHandle_t g_state_mutex;

// Объявления задач
void display_task(void *arg);
void watering_task(void *arg);
void server_task(void *arg);

static esp_lcd_panel_handle_t panel_handle = NULL;
static adc_oneshot_unit_handle_t adc1_handle = NULL;

// Скользящее среднее ADC
static int moisture_samples[MOISTURE_AVG_N];
static int moisture_sample_idx = 0;
static int moisture_sample_sum = 0;
static bool moisture_buf_ready = false;

static void lcd_init(void)
{
    gpio_reset_pin(LCD_BL);
    gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL, 1);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num        = LCD_DC,
        .cs_gpio_num        = LCD_CS,
        .pclk_hz            = 40 * 1000 * 1000,
        .lcd_cmd_bits       = 8,
        .lcd_param_bits     = 8,
        .spi_mode           = 0,
        .trans_queue_depth  = 10,
        .flags = { .dc_low_on_data = 0, .octal_mode = 0 },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_endian     = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 52, 40));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, true));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "LCD %dx%d ok", LCD_WIDTH, LCD_HEIGHT);
}

static int moisture_raw_to_percent(int raw)
{
    if (raw >= MOISTURE_DRY_RAW) return 0;
    if (raw <= MOISTURE_WET_RAW)  return 100;
    return (MOISTURE_DRY_RAW - raw) * 100 / (MOISTURE_DRY_RAW - MOISTURE_WET_RAW);
}

static void moisture_reset_buf(int raw)
{
    for (int i = 0; i < MOISTURE_AVG_N; i++) moisture_samples[i] = raw;
    moisture_sample_sum = raw * MOISTURE_AVG_N;
    moisture_sample_idx = 0;
    moisture_buf_ready = true;
}

static int moisture_add_sample(int raw)
{
    int n = moisture_buf_ready ? MOISTURE_AVG_N : (moisture_sample_idx ? moisture_sample_idx : 1);
    int cur_avg = moisture_sample_sum / n;
    if (abs(raw - cur_avg) > 500) {
        moisture_reset_buf(raw);
        return raw;
    }
    moisture_sample_sum -= moisture_samples[moisture_sample_idx];
    moisture_samples[moisture_sample_idx] = raw;
    moisture_sample_sum += raw;
    moisture_sample_idx = (moisture_sample_idx + 1) % MOISTURE_AVG_N;
    if (moisture_sample_idx == 0) moisture_buf_ready = true;
    return moisture_sample_sum / (moisture_buf_ready ? MOISTURE_AVG_N
                                  : (moisture_sample_idx ? moisture_sample_idx : 1));
}

void app_main(void)
{
    // Удерживаем питание
    gpio_reset_pin(HOLD_PIN);
    gpio_set_direction(HOLD_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(HOLD_PIN, 1);

    // Глобальное состояние
    g_state_mutex = xSemaphoreCreateMutex();
    g_state = (AppState){
        .humidity_pct       = 0,
        .light_on           = false,
        .pump_on            = false,
        .watering_mode      = WATER_MODE,
        .water_interval_h   = WATER_INTERVAL_H,
        .water_threshold_pct = WATER_THRESHOLD,
        .pump_duration_s    = PUMP_DURATION_S,
        .last_watered_s     = 0,
    };

    // LCD
    lcd_init();
    gfx_init(panel_handle, LCD_WIDTH, LCD_HEIGHT);
    gfx_clear(COLOR_BLACK);

    // GPIO
    gpio_reset_pin(BTN_A_PIN);
    gpio_set_direction(BTN_A_PIN, GPIO_MODE_INPUT);

    gpio_reset_pin(BTN_B_PIN);
    gpio_set_direction(BTN_B_PIN, GPIO_MODE_INPUT);

    gpio_reset_pin(OUT_G26);
    gpio_set_direction(OUT_G26, GPIO_MODE_OUTPUT);
    gpio_set_level(OUT_G26, 0);

    gpio_reset_pin(OUT_G25);
    gpio_set_direction(OUT_G25, GPIO_MODE_OUTPUT);
    gpio_set_level(OUT_G25, 0);

    // ADC
    adc_oneshot_unit_init_cfg_t adc_unit_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_unit_cfg, &adc1_handle));
    adc_oneshot_chan_cfg_t adc_chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, MOISTURE_ADC_CHANNEL, &adc_chan_cfg));

    // Часовой пояс — до SNTP, чтобы localtime() сразу работал правильно
    setenv("TZ", TIMEZONE, 1);
    tzset();

    // Wi-Fi + SNTP
    if (wifi_connect()) {
        sntp_sync_start();
        if (sntp_wait_sync(10000)) {
            // Отсчёт интервала полива начинается с момента загрузки,
            // чтобы автополив не сработал сразу после старта.
            STATE_LOCK();
            g_state.last_watered_s = (int64_t)time(NULL);
            STATE_UNLOCK();
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi failed, continuing without time sync");
    }

    // FreeRTOS задачи
    xTaskCreate(display_task,   "display",   4096, NULL, 5, NULL);
    xTaskCreate(watering_task,  "watering",  4096, NULL, 4, NULL);
    xTaskCreate(server_task,    "server",    8192, NULL, 3, NULL);

    // Главный цикл: кнопки + влажность
    int prev_btn_a = 1, prev_btn_b = 1;
    int out_g26 = 0;
    TickType_t last_a = 0, last_b = 0, last_moisture = 0;
    TickType_t pump_start_tick    = 0;
    TickType_t pump_duration_tick = 0;  // 0 = ручной таймер не активен

    while (1) {
        TickType_t now = xTaskGetTickCount();

        int btn_a = gpio_get_level(BTN_A_PIN);
        int btn_b = gpio_get_level(BTN_B_PIN);

        // BTN_A → свет (G26), toggle
        if (prev_btn_a == 1 && btn_a == 0 &&
            (now - last_a) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
            last_a = now;
            out_g26 = !out_g26;
            gpio_set_level(OUT_G26, out_g26);
            STATE_LOCK();
            g_state.light_on = out_g26;
            STATE_UNLOCK();
            ESP_LOGI(TAG, "light: %d", out_g26);
        }

        // BTN_B → ручной полив: включить насос на pump_duration_s секунд
        if (prev_btn_b == 1 && btn_b == 0 &&
            (now - last_b) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
            last_b = now;
            STATE_LOCK();
            int dur = g_state.pump_duration_s;
            STATE_UNLOCK();
            gpio_set_level(OUT_G25, 1);
            pump_start_tick    = now;
            pump_duration_tick = pdMS_TO_TICKS((uint32_t)dur * 1000);
            STATE_LOCK();
            g_state.pump_on = true;
            STATE_UNLOCK();
            ESP_LOGI(TAG, "pump: manual start, %ds", dur);
        }

        // Авто-остановка ручного полива по истечении таймера
        if (pump_duration_tick > 0 &&
            (now - pump_start_tick) >= pump_duration_tick) {
            pump_duration_tick = 0;
            gpio_set_level(OUT_G25, 0);
            STATE_LOCK();
            g_state.pump_on = false;
            STATE_UNLOCK();
            ESP_LOGI(TAG, "pump: auto-stop");
        }

        prev_btn_a = btn_a;
        prev_btn_b = btn_b;

        // Влажность — каждые 500 мс
        if ((now - last_moisture) >= pdMS_TO_TICKS(500)) {
            last_moisture = now;
            int raw = 0;
            if (adc_oneshot_read(adc1_handle, MOISTURE_ADC_CHANNEL, &raw) == ESP_OK) {
                int avg = moisture_add_sample(raw);
                int pct = moisture_raw_to_percent(avg);
                STATE_LOCK();
                g_state.humidity_pct = pct;
                STATE_UNLOCK();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
