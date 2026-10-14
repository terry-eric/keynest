idf_component_register(
    SRCS "app_main.c" "controller.c" "app_logic.c" "audio_worker.c" "mic_monitor.c" "recorder.c" "flash_store.c" "key_sound.c" "device_control.c" "macro_store.c" "usb_keyboard.c" "usb_control.c" "web_server.c"
    INCLUDE_DIRS "."
    REQUIRES pcb2_board esp_timer nvs_flash esp_partition esp_wifi esp_netif esp_event esp_http_server json mbedtls usb esp_psram
    EMBED_TXTFILES "web/index.html" "web/app.js" "web/style.css"
)
