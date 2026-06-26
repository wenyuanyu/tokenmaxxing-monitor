#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "user_app.h"
#include "battery.h"
#include "ble_app.h"
#include "shtc3.h"
#include "ui_app.h"
#include "lvgl_bsp.h"

static const char *TAG = "user_app";

static void env_task(void *arg)
{
    (void) arg;
    for (;;) {
        float t = 0, h = 0;
        int battery_pct = 0;
        bool ok = (shtc3_read(&t, &h) == ESP_OK);
        bool battery_ok = (battery_read(&battery_pct, NULL) == ESP_OK);
        if (Lvgl_lock(-1)) {
            ui_app_set_env(t, h, ok);
            ui_app_set_battery(battery_pct, battery_ok, true);
            Lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void on_ble_data(const usage_report_t *report)
{
    if (Lvgl_lock(200)) {
        ui_app_update(report);
        Lvgl_unlock();
    }
}

void UserApp_AppInit(void)
{
    if (shtc3_init() != ESP_OK) ESP_LOGW(TAG, "shtc3 init failed");
    if (battery_init() != ESP_OK) ESP_LOGW(TAG, "battery init failed");
}

void UserApp_UiInit(void)
{
    ui_app_init();
}

void UserApp_TaskInit(void)
{
    ESP_ERROR_CHECK(ble_app_init(on_ble_data));
    xTaskCreatePinnedToCore(env_task, "env", 4 * 1024, NULL, 3, NULL, 1);
}
