#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
enum { REC_IDLE, REC_STARTING, REC_ACTIVE, REC_STOPPING, REC_ERROR };
enum { REC_BROWSER, REC_FLASH, REC_SD };
typedef struct {
    bool sd_ready, flash_ready, mounting, downloading;
    unsigned state, target, last_target;
    uint32_t frames, dropped_frames;
    char filename[16], error[80];
} recorder_status_t;
esp_err_t recorder_start_worker(void);
esp_err_t recorder_begin(unsigned target, const char *owner);
esp_err_t recorder_stop(void);
esp_err_t recorder_retry_mount(void);
void recorder_feed(const int16_t *samples, size_t count);
void recorder_snapshot(recorder_status_t *status);
esp_err_t recorder_download_begin(char filename[16]);
esp_err_t recorder_download_read(size_t offset, void *buffer, size_t capacity, size_t *count);
esp_err_t recorder_stream_read(const char *owner, int16_t *buffer, size_t capacity, size_t *count);
void recorder_download_end(void);
