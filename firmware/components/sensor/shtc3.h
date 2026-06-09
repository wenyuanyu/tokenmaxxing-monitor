#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "esp_err.h"
#include <stdbool.h>

// SDA=13 SCL=14 (board I2C), SHTC3 @ 0x70.
esp_err_t shtc3_init(void);
// Reads temperature (°C) and relative humidity (%). Returns ESP_OK on success.
esp_err_t shtc3_read(float *temp_c, float *humidity);

#ifdef __cplusplus
}
#endif
