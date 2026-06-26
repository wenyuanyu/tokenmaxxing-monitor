#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>

#include "user_app.h"
#include "battery.h"
#include "battery_log.h"
#include "ble_app.h"
#include "shtc3.h"
#include "ui_app.h"
#include "lvgl_bsp.h"

static const char *TAG = "user_app";

#define STATUS_REFRESH_MS 3000
#define BATTERY_LOG_INTERVAL_MS 30000
#define BATTERY_USB_RISE_SAMPLES 20
#define USER_KEY_GPIO GPIO_NUM_18
#define USER_KEY_POLL_MS 20
#define USER_KEY_DEBOUNCE_SAMPLES 3

static int smooth_battery_percent(int raw_percent, bool usb_connected)
{
    static int shown_percent = -1;
    static int usb_rise_samples = 0;

    if (shown_percent < 0) {
        shown_percent = raw_percent;
        return shown_percent;
    }

    if (usb_connected && raw_percent > shown_percent) {
        if (++usb_rise_samples >= BATTERY_USB_RISE_SAMPLES) {
            shown_percent++;
            usb_rise_samples = 0;
        }
        return shown_percent;
    }

    usb_rise_samples = 0;
    shown_percent = raw_percent;
    return shown_percent;
}

static void env_task(void *arg)
{
    (void) arg;
    TickType_t last_log_tick = 0;
    for (;;) {
        float t = 0, h = 0;
        int battery_pct = 0;
        int battery_mv = 0;
        bool usb_connected = usb_serial_jtag_is_connected();
        bool ok = (shtc3_read(&t, &h) == ESP_OK);
        bool battery_ok = (battery_read(&battery_pct, &battery_mv) == ESP_OK);
        int raw_battery_pct = battery_pct;
        if (battery_ok) {
            battery_pct = smooth_battery_percent(battery_pct, usb_connected);
        }
        if (Lvgl_lock(-1)) {
            ui_app_set_env(t, h, ok);
            ui_app_set_battery(battery_pct, battery_ok, usb_connected);
            Lvgl_unlock();
        }

        TickType_t now = xTaskGetTickCount();
        if (battery_ok &&
            (last_log_tick == 0 ||
             pdTICKS_TO_MS(now - last_log_tick) >= BATTERY_LOG_INTERVAL_MS)) {
            int16_t temp_c_x10 = ok ? (int16_t)(t * 10.0f) : (int16_t)INT16_MIN;
            uint16_t humidity_x10 = ok ? (uint16_t)(h * 10.0f) : (uint16_t)UINT16_MAX;
            battery_log_sample_t sample = {
                .uptime_s = (uint32_t)(pdTICKS_TO_MS(now) / 1000),
                .millivolts = (uint16_t)battery_mv,
                .raw_percent = (uint8_t)raw_battery_pct,
                .shown_percent = (uint8_t)battery_pct,
                .usb_connected = usb_connected,
                .temp_c_x10 = temp_c_x10,
                .humidity_x10 = humidity_x10,
            };
            esp_err_t err = battery_log_append(&sample);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "battery log append failed: %s", esp_err_to_name(err));
            }
            last_log_tick = now;
        }

        vTaskDelay(pdMS_TO_TICKS(STATUS_REFRESH_MS));
    }
}

static void on_ble_data(const usage_report_t *report)
{
    if (Lvgl_lock(200)) {
        ui_app_update(report);
        Lvgl_unlock();
    }
}

static void key_task(void *arg)
{
    (void) arg;
    bool was_pressed = false;
    int stable_count = 0;

    for (;;) {
        bool pressed = (gpio_get_level(USER_KEY_GPIO) == 0);
        if (pressed == was_pressed) {
            stable_count = 0;
        } else if (++stable_count >= USER_KEY_DEBOUNCE_SAMPLES) {
            was_pressed = pressed;
            stable_count = 0;
            if (pressed && Lvgl_lock(200)) {
                ui_app_toggle_activity();
                Lvgl_unlock();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(USER_KEY_POLL_MS));
    }
}

static void key_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << USER_KEY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

void UserApp_AppInit(void)
{
    if (shtc3_init() != ESP_OK) ESP_LOGW(TAG, "shtc3 init failed");
    if (battery_init() != ESP_OK) ESP_LOGW(TAG, "battery init failed");
    if (battery_log_init() != ESP_OK) ESP_LOGW(TAG, "battery log init failed");
}

void UserApp_UiInit(void)
{
    ui_app_init();
}

void UserApp_TaskInit(void)
{
    key_init();
    ESP_ERROR_CHECK(ble_app_init(on_ble_data));
    xTaskCreatePinnedToCore(env_task, "env", 4 * 1024, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(key_task, "key", 2 * 1024, NULL, 4, NULL, 1);
}
