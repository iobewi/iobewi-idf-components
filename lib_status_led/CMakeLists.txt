idf_component_register(
    SRCS "src/lib_status_led.c"
    INCLUDE_DIRS "include"
    REQUIRES
        drv_led_rgb
        esp_driver_gpio
)
