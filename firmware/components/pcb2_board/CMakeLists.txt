idf_component_register(
    SRCS "board.c" "audio.c" "storage.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2s esp_driver_spi esp_driver_sdspi fatfs sdmmc
    PRIV_REQUIRES esp_timer led_strip
)
