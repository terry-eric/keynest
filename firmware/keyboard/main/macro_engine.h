#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>

// Report 1: modifier byte and usages 0..223 as a bitmap; report 2: six media bits.
typedef struct { uint8_t modifiers, keys[28], media; } macro_frame_t;
typedef struct {
    const cJSON *step;
    const unsigned char *text;
    uint8_t code[12], mods[12], count, cursor;
    bool release;
    unsigned mode;
} macro_engine_t;
// Preflight the entire program before sending anything. Caller retains program ownership.
const char *macro_engine_start(macro_engine_t *engine, const cJSON *program);
bool macro_engine_next(macro_engine_t *engine, macro_frame_t *frame, unsigned *hold_ms, bool caps_lock);
void macro_frame_key(macro_frame_t *frame, uint8_t code);
