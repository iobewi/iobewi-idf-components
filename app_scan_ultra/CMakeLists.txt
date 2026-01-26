idf_component_register(
    SRCS "src/app_scan_ultra.c"
    INCLUDE_DIRS "include"
    REQUIRES lib_a02_provider micro_ros_espidf_component
    PRIV_REQUIRES esp_timer
)
