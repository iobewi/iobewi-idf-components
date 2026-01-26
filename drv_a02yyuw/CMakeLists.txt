idf_component_register(
    SRCS "src/drv_a02yyuw.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_uart esp_driver_gpio
)
