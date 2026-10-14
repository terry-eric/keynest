#pragma once
#include "cJSON.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define MACRO_PROGRAM_BYTES 1200
#define MACRO_STEPS 16
#define MACRO_CHORD_KEYS 16
bool macro_program_valid(const cJSON *program);
bool macro_json_safe(const char *raw, size_t length);
esp_err_t macro_store_init(void);
esp_err_t macro_store_command(const char *path, cJSON *body);
cJSON *macro_store_get(unsigned key);
cJSON *macro_store_event(void);
cJSON *macro_store_status(void);
uint8_t macro_store_mask(void);
void macro_store_press(unsigned key);
