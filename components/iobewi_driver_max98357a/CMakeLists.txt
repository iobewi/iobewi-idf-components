idf_component_register(
    SRCS "src/drv_max98357a.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_driver_gpio
        esp_driver_i2s
)
