#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool stable;
    bool candidate;
    uint64_t changed_at;
} pcb2_debounce_t;

static inline int pcb2_debounce_step(pcb2_debounce_t *key, bool raw, uint64_t now_ms) {
    if (raw != key->candidate) {
        key->candidate = raw;
        key->changed_at = now_ms;
    }
    if (key->stable != key->candidate && now_ms - key->changed_at >= 20) {
        key->stable = key->candidate;
        return key->stable ? 1 : -1;
    }
    return 0;
}

static inline uint8_t pcb2_scale_color(uint8_t color, uint8_t brightness) {
    return ((uint32_t)color * brightness + 127) / 255;
}

// Multiplication avoids undefined left shifts of negative signed PCM values.
static inline int32_t pcb2_pcm16_to_slot(int16_t pcm) {
    return (int32_t)pcm * 65536;
}

static inline int32_t pcb2_slot_to_pcm24(int32_t slot) {
    // The unused low 8 bits may float. Arithmetic shift preserves the sign and
    // discards them on the ESP32 GCC toolchain.
    return slot >> 8;
}

static inline int32_t pcb2_stereo_to_pcm24(const int32_t frame[2], bool right) {
    return pcb2_slot_to_pcm24(frame[right ? 1 : 0]);
}
