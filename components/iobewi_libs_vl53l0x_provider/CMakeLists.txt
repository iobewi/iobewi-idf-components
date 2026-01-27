idf_component_register(
    SRCS "src/lib_vl53l0x_provider.c"
    INCLUDE_DIRS "include"
    REQUIRES drv_vl53l0x
    PRIV_REQUIRES esp_timer
)
