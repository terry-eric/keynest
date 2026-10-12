#include "app.h"
#include "recorder.h"
#include "pcb2_board.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>

static SemaphoreHandle_t mutex;
static QueueHandle_t key_events;
static app_snapshot_t state;
static uint32_t revision;
static uint64_t pulses[4];

esp_err_t app_controller_init(void) {
    mutex = xSemaphoreCreateMutex(); key_events = xQueueCreate(16, sizeof(uint8_t));
    if (!mutex || !key_events) return ESP_ERR_NO_MEM;
    logic_defaults(&state.config); state.mic_dbfs = -90;
    nvs_handle_t handle;
    if (nvs_open("pcb2_keypad", NVS_READONLY, &handle) == ESP_OK) {
        keypad_config_t saved; size_t size = sizeof(saved);
        if (nvs_get_blob(handle, "settings", &saved, &size) == ESP_OK && size == sizeof(saved) && logic_config_valid(&saved))
            state.config = saved;
        nvs_close(handle);
    }
    return pcb2_board_init();
}
void app_snapshot(app_snapshot_t *s) {
    xSemaphoreTake(mutex, portMAX_DELAY); *s = state; xSemaphoreGive(mutex);
}
void app_get_config(keypad_config_t *c) {
    xSemaphoreTake(mutex, portMAX_DELAY); *c = state.config; xSemaphoreGive(mutex);
}
esp_err_t app_apply_config(const keypad_config_t *c) {
    if (!logic_config_valid(c)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    state.config = *c; state.dirty = true; ++revision;
    xSemaphoreGive(mutex); return ESP_OK;
}
esp_err_t app_save_config(void) {
    recorder_status_t recording; recorder_snapshot(&recording);
    if ((recording.state != REC_IDLE && recording.state != REC_ERROR) || recording.downloading) return ESP_ERR_INVALID_STATE;
    keypad_config_t config; uint32_t current;
    xSemaphoreTake(mutex, portMAX_DELAY); config = state.config; current = revision; xSemaphoreGive(mutex);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("pcb2_keypad", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "settings", &config, sizeof(config));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (revision == current) state.dirty = false;
        xSemaphoreGive(mutex);
    }
    return err;
}
esp_err_t app_trigger_key(unsigned key) {
    if (key >= 4) return ESP_ERR_INVALID_ARG;
    uint8_t event = key;
    if (xQueueSend(key_events, &event, 0) == pdTRUE) return ESP_OK;
    xSemaphoreTake(mutex, portMAX_DELAY); ++state.queue_drops; xSemaphoreGive(mutex);
    return ESP_ERR_TIMEOUT;
}
void app_audio_meter(float dbfs, float peak) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    state.audio_ready = true; state.mic_dbfs = dbfs; state.mic_peak = peak;
    xSemaphoreGive(mutex);
}
void app_controller_run(void *unused) {
    (void)unused;
    TickType_t wake = xTaskGetTickCount();
    uint64_t last_led = 0;
    for (;;) {
        pcb2_keys_t keys = pcb2_keys_poll();
        for (unsigned i = 0; i < 4; ++i) if (keys.pressed & (1U << i)) app_trigger_key(i);
        keypad_config_t config; app_get_config(&config);
        usb_keyboard_tick(keys.down, &config);
        uint64_t now = esp_timer_get_time() / 1000;
        uint8_t event;
        while (xQueueReceive(key_events, &event, 0) == pdTRUE) {
            pulses[event] = now;
            esp_err_t err = audio_trigger(event, config.sound);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_LOGW("keypad", "Sound queue busy");
        }
        xSemaphoreTake(mutex, portMAX_DELAY); state.keys = keys.down; float peak = state.mic_peak; xSemaphoreGive(mutex);
        if (now - last_led >= 25) {
            last_led = now;
            uint8_t rgb[4][3]; logic_led_frame(&config, now, pulses, keys.down, peak, rgb);
            pcb2_led_brightness(config.brightness);
            for (unsigned i = 0; i < 4; ++i) pcb2_led_set(i, rgb[i][0], rgb[i][1], rgb[i][2]);
            esp_err_t err = pcb2_led_show();
            if (err != ESP_OK) ESP_LOGW("keypad", "LED: %s", esp_err_to_name(err));
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(5));
    }
}
