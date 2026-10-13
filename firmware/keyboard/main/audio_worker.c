#include "app.h"
#include "recorder.h"
#include "key_sound.h"
#include "mic_monitor.h"
#include "pcb2_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>
typedef struct { uint8_t key, effect; } sound_command_t;
static QueueHandle_t commands;

esp_err_t audio_trigger(unsigned key, unsigned effect) {
    if (!commands) return ESP_ERR_INVALID_STATE;
    sound_command_t command = {.key = key, .effect = effect};
    return xQueueSend(commands, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
static void worker(void *unused) {
    (void)unused;
    keypad_voice_t voices[4] = {0};
    int32_t mic[128]; int16_t speaker[128], capture[128];
    double sum = 0; size_t measured = 0; float peak = 0;
    unsigned decimation = 0; int32_t average = 0;
    for (;;) {
        sound_command_t command;
        while (xQueueReceive(commands, &command, 0) == pdTRUE) {
            logic_voice_start(&voices[command.key % 4], command.key, command.effect);
            if (command.effect == SOUND_CUSTOM) key_sound_trigger(command.key % 4);
            else key_sound_stop(command.key % 4);
        }
        keypad_config_t config; app_get_config(&config);
        size_t count = 0;
        esp_err_t err = pcb2_audio_read(mic, 128, &count, 100);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW("audio", "RX: %s", esp_err_to_name(err)); vTaskDelay(pdMS_TO_TICKS(20));
        }
        int16_t recording[43]; size_t recording_count = 0;
        for (size_t i = 0; i < count; ++i) {
            capture[i] = logic_mic_pcm16(mic[i], config.mic_gain);
            float value = capture[i] / 32768.0f;
            sum += value * value; ++measured; peak = fmaxf(peak, fabsf(value));
            speaker[i] = logic_sound_sample(voices, config.volume);
            average += capture[i];
            if (++decimation == 3) {
                recording[recording_count++] = average / 3;
                average = 0; decimation = 0;
            }
        }
        key_sound_mix(speaker, count, config.volume);
        if (recording_count) {
            mic_monitor_feed(recording, recording_count);
            recorder_feed(recording, recording_count);
        }
        size_t offset = 0;
        while (offset < count) {
            size_t written = 0;
            err = pcb2_audio_write(speaker + offset, count - offset, &written, 100); offset += written;
            if (err != ESP_OK || !written) break;
        }
        if (measured >= KEYPAD_RATE / 10) {
            app_audio_meter(20 * log10(fmax(sqrt(sum / measured), 0.00003162)), peak);
            measured = 0; sum = 0; peak = 0;
        }
    }
}
esp_err_t audio_worker_start(void) {
    esp_err_t err = pcb2_audio_init(KEYPAD_RATE);
    if (err != ESP_OK) return err;
    commands = xQueueCreate(16, sizeof(sound_command_t));
    if (!commands || xTaskCreatePinnedToCore(worker, "keypad-audio", 6144, NULL, 6, NULL, 1) != pdPASS) {
        if (commands) vQueueDelete(commands);
        commands = NULL; pcb2_audio_deinit(); return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
