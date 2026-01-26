idf_component_register(
    SRCS "src/lib_a02_provider.c"
    INCLUDE_DIRS "include"
    REQUIRES drv_a02yyuw
    PRIV_REQUIRES esp_timer
)
