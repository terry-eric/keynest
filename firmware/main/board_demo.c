#include "pcb2_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <dirent.h>
#include <math.h>

static const char *TAG = "pcb2-demo";
static QueueHandle_t audio_commands;
static QueueHandle_t sd_commands;
enum { COMMAND_BEEP = 1, COMMAND_METER };

static void audio_task(void *unused) {
    (void)unused;
    int32_t mic[PCB2_AUDIO_BLOCK_FRAMES];
    int16_t speaker[PCB2_AUDIO_BLOCK_FRAMES];
    bool meter = false;
    unsigned tone_left = 0, tone_position = 0, report_blocks = 0;
    double sum = 0;
    size_t measured = 0;
    for (;;) {
        int command;
        while (xQueueReceive(audio_commands, &command, 0) == pdTRUE) {
            if (command == COMMAND_BEEP) { tone_left = 9600; tone_position = 0; }
            if (command == COMMAND_METER) { meter = !meter; sum = 0; measured = 0; }
        }
        size_t frames = 0;
        esp_err_t err = pcb2_audio_read(mic, PCB2_AUDIO_BLOCK_FRAMES, &frames, 100);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "Microphone read: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (!frames) continue;
        for (size_t i = 0; i < frames; ++i) {
            if (meter) { double sample = mic[i] / 8388608.0; sum += sample * sample; ++measured; }
            speaker[i] = 0;
            if (tone_left) {
                float envelope = fminf(1.0f, fminf(tone_position / 480.0f, tone_left / 480.0f));
                speaker[i] = (int16_t)(1000.0f * envelope * sinf(6.2831853f * 440 * tone_position / 48000));
                ++tone_position; --tone_left;
            }
        }
        size_t offset = 0;
        while (offset < frames) {
            size_t written = 0;
            err = pcb2_audio_write(speaker + offset, frames - offset, &written, 100);
            offset += written;
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Speaker write: %s (%u frames)", esp_err_to_name(err), (unsigned)written);
                break;
            }
            if (!written) break;
        }
        if (++report_blocks >= 188) {
            report_blocks = 0;
            if (meter && measured) {
                double rms = sqrt(sum / measured);
                ESP_LOGI(TAG, "Microphone RMS %.5f, %.1f dBFS", rms, 20 * log10(fmax(rms, 1e-9)));
                sum = 0; measured = 0;
            }
        }
    }
}

static void inspect_card(void) {
    if (pcb2_sd_is_mounted()) { ESP_LOGI(TAG, "SD already mounted at /sdcard"); return; }
    esp_err_t err = pcb2_sd_mount();
    if (err != ESP_OK) { ESP_LOGW(TAG, "SD mount: %s (card is optional)", esp_err_to_name(err)); return; }
    DIR *directory = opendir("/sdcard");
    if (!directory) { ESP_LOGW(TAG, "Cannot list /sdcard"); return; }
    struct dirent *entry;
    unsigned shown = 0;
    while (shown < 20 && (entry = readdir(directory))) {
        ESP_LOGI(TAG, "SD: %s", entry->d_name); ++shown;
    }
    closedir(directory);
    ESP_LOGI(TAG, "SD mounted at /sdcard; no test file was written");
}

static void sd_task(void *unused) {
    (void)unused;
    int command;
    for (;;) {
        if (xQueueReceive(sd_commands, &command, portMAX_DELAY) == pdTRUE) inspect_card();
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "PCB2 S3 hardware demo; pin map source: 2026-09-19");
    ESP_ERROR_CHECK(pcb2_board_init());
    sd_commands = xQueueCreate(1, sizeof(int));
    if (!sd_commands || xTaskCreate(sd_task, "pcb2-sd", 6144, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot start SD worker");
        if (sd_commands) vQueueDelete(sd_commands);
        sd_commands = NULL;
    } else {
        int command = 1;
        xQueueSend(sd_commands, &command, 0);
    }
    esp_err_t err = pcb2_audio_init(PCB2_AUDIO_DEFAULT_RATE);
    if (err == ESP_OK) {
        audio_commands = xQueueCreate(8, sizeof(int));
        if (!audio_commands || xTaskCreate(audio_task, "pcb2-audio", 6144, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Cannot start audio worker");
            if (audio_commands) vQueueDelete(audio_commands);
            audio_commands = NULL;
            pcb2_audio_deinit();
        }
    } else ESP_LOGE(TAG, "Audio init: %s", esp_err_to_name(err));
    ESP_LOGI(TAG, "KEY1: quiet 440Hz tone; KEY2: mic meter; KEY3: retry SD; KEY4: RGB color");
    unsigned color = 0;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        pcb2_keys_t keys = pcb2_keys_poll();
        if (keys.pressed || keys.released)
            ESP_LOGI(TAG, "Keys down=0x%02x press=0x%02x release=0x%02x", keys.down, keys.pressed, keys.released);
        if (audio_commands) {
            int command = COMMAND_BEEP;
            if ((keys.pressed & 1) && xQueueSend(audio_commands, &command, 0) != pdTRUE)
                ESP_LOGW(TAG, "Audio command queue full");
            command = COMMAND_METER;
            if ((keys.pressed & 2) && xQueueSend(audio_commands, &command, 0) != pdTRUE)
                ESP_LOGW(TAG, "Audio command queue full");
        }
        if ((keys.pressed & 4) && sd_commands) {
            int command = 1;
            if (xQueueSend(sd_commands, &command, 0) != pdTRUE) ESP_LOGW(TAG, "SD request pending");
        }
        if (keys.pressed & 8) color = (color + 1) % 3;
        for (size_t i = 0; i < PCB2_LED_COUNT; ++i) {
            uint8_t value = (keys.down & (1U << i)) ? 255 : 32;
            pcb2_led_set(i, color == 0 ? value : 0, color == 1 ? value : 0, color == 2 ? value : 0);
        }
        err = pcb2_led_show();
        if (err != ESP_OK) ESP_LOGE(TAG, "LED update: %s", esp_err_to_name(err));
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(5));
    }
}
