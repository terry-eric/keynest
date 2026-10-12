#pragma once
#include <stddef.h>
#include <stdint.h>

#define MIC_MONITOR_FRAMES 1024

typedef struct {
    size_t count;
    uint64_t frames;
    int64_t captured_us;
} mic_monitor_snapshot_t;

// A read-only window of recent PCM; independent of recording destinations.
void mic_monitor_feed(const int16_t *pcm, size_t count);
void mic_monitor_read(int16_t *pcm, size_t capacity, mic_monitor_snapshot_t *snapshot);
