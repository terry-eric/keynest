#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define KEY_SOUND_MAX_FRAMES 32000U
typedef struct {
    bool ready, uploading;
    unsigned storage;
    uint32_t frames, crc32, plays[4], rendered[4];
    char filename[16], error[80];
} key_sound_status_t;
esp_err_t key_sound_init(void);
void key_sound_snapshot(key_sound_status_t *out);
bool key_sound_busy(void);
void key_sound_trigger(unsigned key);
void key_sound_stop(unsigned key);
void key_sound_mix(int16_t *output, size_t count, unsigned volume);
esp_err_t key_sound_begin(const char *owner, unsigned storage, uint32_t frames, uint32_t crc);
esp_err_t key_sound_chunk(const char *owner, uint32_t offset, const uint8_t *bytes, size_t count, uint32_t crc);
esp_err_t key_sound_commit(const char *owner);
esp_err_t key_sound_cancel(const char *owner);
