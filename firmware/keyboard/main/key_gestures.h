#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { KEY_GESTURE_NONE, KEY_GESTURE_TAP, KEY_GESTURE_HOLD, KEY_GESTURE_DOUBLE };
typedef struct {
    uint64_t pressed_at[4], released_at[4];
    uint8_t previous, pending, second, held;
} key_gestures_t;
void key_gestures_reset(key_gestures_t *state, uint8_t down);
void key_gestures_tick(key_gestures_t *state, uint8_t down, uint64_t now,
                       const uint16_t hold_ms[4], const uint16_t double_ms[4], uint8_t events[4]);
