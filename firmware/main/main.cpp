// Qwen Code BLE token monitor for Waveshare ESP32-S3-RLCD-4.2.

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include "nvs_flash.h"

#include "display_bsp.h"
#include "lvgl_bsp.h"
#include "user_app.h"
#include "user_config.h"

DisplayPort RlcdPort(RLCD_MOSI_PIN, RLCD_SCK_PIN, RLCD_DC_PIN, RLCD_CS_PIN, RLCD_RST_PIN, LCD_WIDTH, LCD_HEIGHT);

static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static int activity_level_from_color(uint16_t px)
{
    if (px == rgb565(0x11, 0x00, 0x11)) return 1;
    if (px == rgb565(0x22, 0x00, 0x22)) return 2;
    if (px == rgb565(0x33, 0x00, 0x33)) return 3;
    if (px == rgb565(0x44, 0x00, 0x44)) return 4;
    return -1;
}

static uint8_t activity_pattern_color(int level, int x, int y)
{
    int lx = (x - 52) % 12;
    int ly = (y - 124) % 12;
    if (lx < 0) lx += 12;
    if (ly < 0) ly += 12;

    bool black = false;
    if (level <= 0) {
        black = false;                         // ' ' all white
    } else if (level == 1) {
        black = ((lx + ly) % 3) == 2;          // '░' about 1/3 black
    } else if (level == 2) {
        black = ((lx + ly) & 1) == 1;          // '▒' 1/2 black
    } else if (level == 3) {
        black = ((lx + ly) % 3) != 2;          // '▓' about 2/3 black
    } else if (level >= 4) {
        black = true;                          // '█' all black
    }
    return black ? ColorBlack : ColorWhite;
}

static void Lvgl_FlushCallback(lv_display_t *drv, const lv_area_t *area, uint8_t *color_map)
{
    uint16_t *buffer = (uint16_t *) color_map;
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            int activity_level = activity_level_from_color(*buffer);
            uint8_t color = ColorWhite;
            if (activity_level >= 0) {
                color = activity_pattern_color(activity_level, x, y);
            } else if (*buffer < 0x7fff) {
                color = ColorBlack;
            }
            RlcdPort.RLCD_SetPixel(x, y, color);
            buffer++;
        }
    }
    RlcdPort.RLCD_Display();
    lv_disp_flush_ready(drv);
}

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Bring up the display + UI first so BLE startup status is visible.
    RlcdPort.RLCD_Init();
    Lvgl_PortInit(LCD_WIDTH, LCD_HEIGHT, Lvgl_FlushCallback);
    if (Lvgl_lock(-1)) {
        UserApp_UiInit();
        Lvgl_unlock();
    }

    UserApp_AppInit();
    UserApp_TaskInit();
}
