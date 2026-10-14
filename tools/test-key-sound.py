"""Run the real key_sound.c against simulated NOR/NVS, without connecting USB."""
import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler',default=shutil.which('cc') or shutil.which('gcc') or str(ROOT/'.tools/tcc/tcc/tcc.exe'))
args=parser.parse_args()
platform=r'''#ifndef PCB2_SOUND_TEST_PLATFORM_H
#define PCB2_SOUND_TEST_PLATFORM_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FOUND 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_INVALID_CRC 5
#define ESP_ERR_INVALID_RESPONSE 6
#define ESP_ERR_NOT_SUPPORTED 7
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 255
typedef struct { size_t size; } esp_partition_t;
const esp_partition_t *esp_partition_find_first(int,int,const char*);
esp_err_t esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
esp_err_t esp_partition_write(const esp_partition_t*,size_t,const void*,size_t);
esp_err_t esp_partition_erase_range(const esp_partition_t*,size_t,size_t);
const char *esp_err_to_name(esp_err_t);
int64_t esp_timer_get_time(void);
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static inline void *heap_caps_malloc(size_t n,unsigned flags){(void)flags;return malloc(n);}
#define ESP_LOGW(...) ((void)0)
typedef int SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffff
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
static inline int xSemaphoreCreateMutex(void){return 1;}
static inline int xSemaphoreTake(int m,unsigned delay){(void)m;(void)delay;return 1;}
static inline void xSemaphoreGive(int m){(void)m;}
static inline void vTaskDelay(unsigned t){(void)t;}
static inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*n,unsigned stack,void*a,unsigned priority,void*b,int core){(void)fn;(void)n;(void)stack;(void)a;(void)priority;(void)b;(void)core;return 1;}
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_open(const char*,int,nvs_handle_t*);
void nvs_close(nvs_handle_t);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_get_u8(nvs_handle_t,const char*,uint8_t*);
esp_err_t nvs_set_u8(nvs_handle_t,const char*,uint8_t);
esp_err_t nvs_get_u32(nvs_handle_t,const char*,uint32_t*);
esp_err_t nvs_set_u32(nvs_handle_t,const char*,uint32_t);
esp_err_t nvs_get_str(nvs_handle_t,const char*,char*,size_t*);
esp_err_t nvs_set_str(nvs_handle_t,const char*,const char*);
#endif
'''
tiny_math=r'''#pragma once
double sin(double);double cos(double);double exp(double);double fmod(double,double);double fabs(double);
#define sinf(x) ((float)sin(x))
#define cosf(x) ((float)cos(x))
#define expf(x) ((float)exp(x))
#define fmodf(x,y) ((float)fmod(x,y))
#define fabsf(x) ((float)fabs(x))
static inline float fminf(float a,float b){return a<b?a:b;}
static inline float fmaxf(float a,float b){return a>b?a:b;}
'''
cache=ROOT/'.tools';cache.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='sound-tests-',dir=cache) as tmp:
    folder=Path(tmp);(folder/'freertos').mkdir()
    assert folder.resolve().is_relative_to(cache.resolve())
    (folder/'platform.h').write_text(platform)
    for header in ['esp_err.h','esp_partition.h','esp_timer.h','esp_heap_caps.h','esp_log.h','nvs.h','freertos/FreeRTOS.h','freertos/task.h','freertos/semphr.h']:
        (folder/header).write_text('#include "platform.h"\n')
    compiler=Path(args.compiler);tiny=compiler.name.lower().startswith('tcc')
    if tiny:
        (folder/'math.h').write_text(tiny_math)
        (folder/'unistd.h').write_text('#include <io.h>\nint _commit(int);\n')
    output=folder/('test.exe' if tiny else 'test')
    for source in ['test-key-sound.c','test-keypad-logic.c']:
        command=[str(compiler),'-I',str(folder),'-o',str(output),str(ROOT/'tools'/source),str(ROOT/'firmware/keyboard/main/app_logic.c')]
        command+=['-Dfsync=_commit'] if tiny else ['-lm']
        subprocess.run(command,check=True,cwd=ROOT)
        subprocess.run([str(output)],check=True,cwd=ROOT)
