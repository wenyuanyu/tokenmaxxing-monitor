#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t uptime_s;
    uint16_t millivolts;
    uint8_t raw_percent;
    uint8_t shown_percent;
    bool usb_connected;
    int16_t temp_c_x10;
    uint16_t humidity_x10;
} battery_log_sample_t;

esp_err_t battery_log_init(void);
esp_err_t battery_log_append(const battery_log_sample_t *sample);

#ifdef __cplusplus
}
#endif
