#include "app.h"
#include "recorder.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void) {
    // Do not erase user settings automatically when NVS reports a storage error.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(app_controller_init());
    ESP_ERROR_CHECK(recorder_start_worker());
    ESP_ERROR_CHECK(usb_keyboard_start());
    esp_err_t audio = audio_worker_start();
    if (audio != ESP_OK) ESP_LOGE("keypad", "Audio unavailable: %s", esp_err_to_name(audio));
    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI("keypad", "USB HID + CDC ready. Hardware pin map: 2026-09-19.");
    if (xTaskCreatePinnedToCore(app_controller_run, "keypad-keys", 6144, NULL, 5, NULL, 1) != pdPASS)
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
}
