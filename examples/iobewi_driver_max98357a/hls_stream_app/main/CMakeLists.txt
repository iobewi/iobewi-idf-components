idf_component_register(
    SRCS
        "main.c"
        "wifi_helper.c"
        "m3u8_parser.c"
        "stream_player.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_http_client
        esp-tls
        nvs_flash
        iobewi_driver_max98357a
        esp_ringbuf
)