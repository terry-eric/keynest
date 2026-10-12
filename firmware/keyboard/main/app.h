#pragma once
#include "app_logic.h"
#include "esp_err.h"
typedef struct {
    keypad_config_t config;
    uint8_t keys;
    bool dirty, audio_ready;
    float mic_dbfs, mic_peak;
    uint32_t queue_drops;
} app_snapshot_t;
esp_err_t app_controller_init(void);
void app_controller_run(void *unused);
void app_snapshot(app_snapshot_t *snapshot);
void app_get_config(keypad_config_t *config);
esp_err_t app_apply_config(const keypad_config_t *config);
esp_err_t app_save_config(void);
esp_err_t app_trigger_key(unsigned key);
void app_audio_meter(float dbfs, float peak);
esp_err_t audio_worker_start(void);
esp_err_t audio_trigger(unsigned key, unsigned effect);
esp_err_t usb_keyboard_start(void);
void usb_keyboard_tick(uint8_t down, const keypad_config_t *config);
bool usb_keyboard_connected(void);
esp_err_t web_server_start(void);
