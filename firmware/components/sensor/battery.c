#include "battery.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#include <stdbool.h>

#define BAT_ADC_UNIT ADC_UNIT_1
#define BAT_ADC_CHANNEL ADC_CHANNEL_3
#define BAT_ADC_ATTEN ADC_ATTEN_DB_12
#define BAT_ADC_BITWIDTH ADC_BITWIDTH_12
#define BAT_DIVIDER_NUM 3
#define BAT_SAMPLE_COUNT 8

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int lipo_percent_from_mv(int mv)
{
    if (mv < 3000) return 0;
    if (mv > 4120) return 100;
    return (mv - 3000) * 100 / (4120 - 3000);
}

esp_err_t battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BAT_ADC_UNIT,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) return err;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BAT_ADC_ATTEN,
        .bitwidth = BAT_ADC_BITWIDTH,
    };
    err = adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) return err;

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BAT_ADC_UNIT,
        .atten = BAT_ADC_ATTEN,
        .bitwidth = BAT_ADC_BITWIDTH,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK);
    return ESP_OK;
}

esp_err_t battery_read(int *percent, int *millivolts)
{
    if (!s_adc) return ESP_ERR_INVALID_STATE;

    int total_mv = 0;
    for (int i = 0; i < BAT_SAMPLE_COUNT; i++) {
        int raw = 0;
        int mv = 0;
        esp_err_t err = adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &raw);
        if (err != ESP_OK) return err;
        if (s_cali_ok) {
            err = adc_cali_raw_to_voltage(s_cali, raw, &mv);
            if (err != ESP_OK) return err;
        } else {
            mv = raw * 3300 / 4095;
        }
        total_mv += mv * BAT_DIVIDER_NUM;
    }

    int bat_mv = total_mv / BAT_SAMPLE_COUNT;
    if (millivolts) *millivolts = bat_mv;
    if (percent) *percent = clamp_int(lipo_percent_from_mv(bat_mv), 0, 100);
    return ESP_OK;
}
