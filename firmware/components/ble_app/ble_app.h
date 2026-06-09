#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "usage_client.h"

typedef void (*ble_data_cb_t)(const usage_report_t *report);

esp_err_t ble_app_init(ble_data_cb_t cb);

#ifdef __cplusplus
}
#endif
