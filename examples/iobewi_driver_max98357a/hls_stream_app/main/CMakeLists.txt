idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_event
        nvs_flash
        esp_audio_codec
        iobewi_driver_max98357a
        iobewi_apps_hls_player
)
