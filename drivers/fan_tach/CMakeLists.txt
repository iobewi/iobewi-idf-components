idf_component_register(
    SRCS "src/drv_fan_tach.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_pcnt esp_driver_gpio
)