idf_component_register(
    SRCS "shtc3.c" "battery.c" "battery_log.c"
    INCLUDE_DIRS "./"
    PRIV_REQUIRES esp_driver_i2c esp_adc esp_partition
)
