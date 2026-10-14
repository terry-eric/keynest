"""Compile real macro_store.c and ESP-IDF cJSON against deterministic host stubs."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
TCC=ROOT/".tools/tcc/tcc/tcc.exe"
CJSON=ROOT/".tools/platformio/packages/framework-espidf/components/json/cJSON"
with tempfile.TemporaryDirectory(prefix="macro-test-",dir=ROOT/".tools") as folder:
    temp=Path(folder);(temp/"freertos").mkdir()
    (temp/"esp_err.h").write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n#define ESP_ERR_NO_MEM 3\n')
    (temp/"esp_timer.h").write_text('#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
    (temp/"nvs.h").write_text('#pragma once\n#include <stddef.h>\ntypedef unsigned nvs_handle_t;\n#define NVS_READONLY 0\n#define NVS_READWRITE 1\nint nvs_open(const char*,int,nvs_handle_t*);\nvoid nvs_close(nvs_handle_t);\nint nvs_get_str(nvs_handle_t,const char*,char*,size_t*);\nint nvs_set_str(nvs_handle_t,const char*,const char*);\nint nvs_commit(nvs_handle_t);\n')
    (temp/"freertos/FreeRTOS.h").write_text('#pragma once\n#define portMAX_DELAY 0xffffffffu\n')
    (temp/"freertos/semphr.h").write_text('#pragma once\ntypedef void *SemaphoreHandle_t;\nvoid *xSemaphoreCreateMutex(void);\nint xSemaphoreTake(void*,unsigned);\nint xSemaphoreGive(void*);\n')
    binary=temp/"test.exe"
    command=[str(TCC),'-DCJSON_HIDE_SYMBOLS','-D__cdecl=','-D__stdcall=','-I'+str(temp),'-I'+str(CJSON),'-I'+str(ROOT/"firmware/keyboard/main"),str(ROOT/"tools/test-macro-store.c"),str(ROOT/"firmware/keyboard/main/macro_store.c"),str(CJSON/"cJSON.c"),'-o',str(binary)]
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
