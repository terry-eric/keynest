#include "mic_monitor.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static int16_t recent[MIC_MONITOR_FRAMES];
static size_t head, available;
static uint64_t frames;
static int64_t captured_us;

void mic_monitor_feed(const int16_t *pcm, size_t count) {
    if (!pcm || !count) return;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    for (size_t i = 0; i < count; ++i) {
        recent[head] = pcm[i];
        head = (head + 1) % MIC_MONITOR_FRAMES;
        if (available < MIC_MONITOR_FRAMES) ++available;
    }
    frames += count;
    captured_us = now;
    portEXIT_CRITICAL(&lock);
}

void mic_monitor_read(int16_t *pcm, size_t capacity, mic_monitor_snapshot_t *snapshot) {
    portENTER_CRITICAL(&lock);
    size_t count = available < capacity ? available : capacity;
    size_t start = (head + MIC_MONITOR_FRAMES - count) % MIC_MONITOR_FRAMES;
    for (size_t i = 0; i < count; ++i) pcm[i] = recent[(start + i) % MIC_MONITOR_FRAMES];
    *snapshot = (mic_monitor_snapshot_t){.count = count, .frames = frames, .captured_us = captured_us};
    portEXIT_CRITICAL(&lock);
}
