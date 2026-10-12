#pragma once
#include "cJSON.h"
#include "esp_err.h"

cJSON *app_state_json(void);
esp_err_t app_command_execute(const char *path, cJSON *body);
esp_err_t usb_control_start(void);
