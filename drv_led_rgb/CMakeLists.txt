idf_component_register(
    SRCS "src/drv_led_rgb.c"
    INCLUDE_DIRS "include"
    REQUIRES
        led_strip
        esp_driver_gpio
)
