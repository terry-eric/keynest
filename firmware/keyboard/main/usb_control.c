#include "app.h"
#include "app_api.h"
#include "mic_monitor.h"
#include "recorder.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define RPC_LINE_BYTES 1700
static bool file_active;
static size_t file_offset;
static int64_t file_touched;

static void send_response(const char *text) {
    size_t length = strlen(text);
    // Serialize with console logs and drain old output before queuing a complete frame.
    flockfile(stdout);
    fflush(stdout);
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(200));
    if (tud_cdc_n_write_available(0) >= length + 11) {
        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)"\nPCB2_RPC ", 10);
        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)text, length);
        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)"\n", 1);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
    }
    funlockfile(stdout);
}

static cJSON *binary_json(const void *pcm, size_t bytes) {
    char encoded[2733]; size_t length = 0;
    if (bytes > 2048 || mbedtls_base64_encode((unsigned char *)encoded, sizeof(encoded), &length, pcm, bytes)) return NULL;
    encoded[length] = 0;
    cJSON *json = cJSON_CreateObject();
    if (json) {
        cJSON_AddStringToObject(json, "pcm", encoded);
        cJSON_AddNumberToObject(json, "bytes", bytes);
        cJSON_AddNumberToObject(json, "crc32", logic_crc32(0, pcm, bytes));
    }
    return json;
}
static bool valid_envelope(cJSON *root) {
    if (!cJSON_IsObject(root)) return false;
    for (cJSON *item = root->child; item; item = item->next) {
        if (!item->string || (strcmp(item->string, "id") && strcmp(item->string, "path") && strcmp(item->string, "body"))) return false;
        for (cJSON *other = item->next; other; other = other->next)
            if (other->string && !strcmp(item->string, other->string)) return false;
    }
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id"), *path = cJSON_GetObjectItemCaseSensitive(root, "path");
    cJSON *body = cJSON_GetObjectItemCaseSensitive(root, "body");
    return cJSON_IsNumber(id) && isfinite(id->valuedouble) && id->valuedouble >= 0 && id->valuedouble <= 2147483647 &&
        floor(id->valuedouble) == id->valuedouble && cJSON_IsString(path) && strlen(path->valuestring) < 40 && (!body || cJSON_IsObject(body));
}
static void handle_line(char *line, size_t length) {
    if (length < 7 || strncmp(line, "PCB2? ", 6)) return;
    cJSON *root = cJSON_ParseWithLengthOpts(line + 6, length - 5, NULL, true);
    if (!valid_envelope(root)) { cJSON_Delete(root); return; }
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id"), *body = cJSON_GetObjectItemCaseSensitive(root, "body");
    const char *path = cJSON_GetObjectItemCaseSensitive(root, "path")->valuestring;
    cJSON *data = NULL; esp_err_t err = ESP_OK;
    if (!strcmp(path, "/api/state") && !body) {
        data = app_state_json();
    } else if (!strcmp(path, "/api/microphone") && !body) {
        int16_t pcm[MIC_MONITOR_FRAMES]; mic_monitor_snapshot_t snapshot;
        mic_monitor_read(pcm, MIC_MONITOR_FRAMES, &snapshot);
        int64_t age = (esp_timer_get_time() - snapshot.captured_us) / 1000;
        if (!snapshot.count || age > 1000) err = ESP_ERR_INVALID_STATE;
        else if ((data = binary_json(pcm, snapshot.count * 2))) {
            cJSON_AddNumberToObject(data, "rate", CAPTURE_RATE);
            cJSON_AddNumberToObject(data, "frames", (double)snapshot.frames);
            cJSON_AddNumberToObject(data, "age_ms", age);
        }
    } else if (!strcmp(path, "/api/audio") && body) {
        cJSON *owner = cJSON_GetObjectItemCaseSensitive(body, "owner"); int16_t pcm[1024]; size_t count = 0;
        err = cJSON_IsString(owner) ? recorder_stream_read(owner->valuestring, pcm, 1024, &count) : ESP_ERR_INVALID_ARG;
        if (err == ESP_OK && (data = binary_json(pcm, count * 2))) {
            recorder_status_t status; recorder_snapshot(&status);
            cJSON_AddNumberToObject(data, "state", status.state); cJSON_AddNumberToObject(data, "frames", status.frames);
            cJSON_AddNumberToObject(data, "dropped", status.dropped_frames);
        }
    } else if (!strcmp(path, "/api/file/begin") && !body) {
        char name[16]; err = file_active ? ESP_ERR_INVALID_STATE : recorder_download_begin(name);
        if (err == ESP_OK) {
            file_active = true; file_offset = 0; file_touched = esp_timer_get_time();
            data = cJSON_CreateObject(); if (data) cJSON_AddStringToObject(data, "filename", name);
        }
    } else if (!strcmp(path, "/api/file/read") && !body) {
        uint8_t bytes[2048]; size_t count = 0;
        err = file_active ? recorder_download_read(file_offset, bytes, sizeof(bytes), &count) : ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) { file_offset += count; file_touched = esp_timer_get_time(); data = binary_json(bytes, count); }
    } else if (!strcmp(path, "/api/file/end") && !body) {
        if (file_active) recorder_download_end();
        file_active = false; data = cJSON_CreateObject();
    } else if (body) {
        err = app_command_execute(path, body);
        if (err == ESP_OK) { data = cJSON_CreateObject(); if (data) cJSON_AddBoolToObject(data, "ok", true); }
    } else err = ESP_ERR_INVALID_ARG;
    if (err == ESP_OK && !data) err = ESP_ERR_NO_MEM;
    cJSON *response = cJSON_CreateObject();
    if (response) {
        cJSON_AddNumberToObject(response, "id", id->valuedouble); cJSON_AddNumberToObject(response, "protocol", 1);
        int status = err == ESP_OK ? 200 : err == ESP_ERR_INVALID_ARG ? 400 : err == ESP_ERR_INVALID_STATE ? 409 : 503;
        cJSON_AddNumberToObject(response, "status", status);
        if (err == ESP_OK) cJSON_AddItemToObject(response, "data", data);
        else { cJSON_Delete(data); cJSON_AddStringToObject(response, "error", esp_err_to_name(err)); }
        char *text = cJSON_PrintUnformatted(response);
        if (text) { send_response(text); free(text); }
        cJSON_Delete(response);
    } else cJSON_Delete(data);
    cJSON_Delete(root);
}
static void worker(void *unused) {
    (void)unused;
    char line[RPC_LINE_BYTES]; uint8_t input[128]; size_t used = 0; bool discard = false;
    for (;;) {
        size_t count = 0;
        if (!tud_cdc_n_connected(0)) { used = 0; discard = false; }
        if (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, input, sizeof(input), &count) == ESP_OK) {
            for (size_t i = 0; i < count; ++i) {
                if (input[i] == '\n') {
                    if (!discard) { line[used] = 0; handle_line(line, used); }
                    used = 0; discard = false;
                } else if (input[i] != '\r') {
                    if (!input[i] || used >= sizeof(line) - 1) discard = true;
                    if (!discard) line[used++] = input[i];
                }
            }
        }
        if (file_active && esp_timer_get_time() - file_touched > 2000000) { recorder_download_end(); file_active = false; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
esp_err_t usb_control_start(void) {
    return xTaskCreatePinnedToCore(worker, "keypad-usb-api", 12288, NULL, 2, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
