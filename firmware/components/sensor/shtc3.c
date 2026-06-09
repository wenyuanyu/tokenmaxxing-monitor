#include "shtc3.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

#define SDA_PIN 13
#define SCL_PIN 14
#define SHTC3_ADDR 0x70

static const char *TAG = "shtc3";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

static esp_err_t cmd(uint16_t c)
{
    uint8_t b[2] = { (uint8_t)(c >> 8), (uint8_t)(c & 0xff) };
    return i2c_master_transmit(s_dev, b, 2, 100);
}

esp_err_t shtc3_init(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = -1,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus, &s_bus);
    if (err != ESP_OK) return err;
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_ADDR,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(s_bus, &dev, &s_dev);
}

esp_err_t shtc3_read(float *temp_c, float *humidity)
{
    cmd(0x3517);               // wakeup
    vTaskDelay(pdMS_TO_TICKS(1));
    if (cmd(0x7866) != ESP_OK) // measure, normal power, T first, no clock stretch
        return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(15));
    uint8_t r[6] = {0};
    esp_err_t err = i2c_master_receive(s_dev, r, 6, 100);
    cmd(0xB098);               // sleep
    if (err != ESP_OK) return err;

    uint16_t rawT = (r[0] << 8) | r[1];
    uint16_t rawH = (r[3] << 8) | r[4];
    if (temp_c)  *temp_c   = -45.0f + 175.0f * (rawT / 65535.0f);
    if (humidity) *humidity = 100.0f * (rawH / 65535.0f);
    return ESP_OK;
}
