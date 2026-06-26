#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdint.h>

esp_err_t battery_init(void);
esp_err_t battery_read(int *percent, int *millivolts);

#ifdef __cplusplus
}
#endif
