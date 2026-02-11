idf_component_register(
    SRCS "src/app_hls_player.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_common
        esp_http_client
        esp_https_ota
        iobewi_libs_m3u8_parser
        esp_audio_codec
        mbedtls
        esp_ringbuf
        esp_timer
)
