#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
esp_err_t flash_store_init(void);
esp_err_t flash_store_save(const int16_t *samples, uint32_t frames);
esp_err_t flash_store_read(size_t offset, void *buffer, size_t capacity, size_t *count);
size_t flash_store_length(void);
