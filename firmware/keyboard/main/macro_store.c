#include "macro_store.h"
#include "nvs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static SemaphoreHandle_t lock, storage_lock;
static char programs[4][MACRO_PROGRAM_BYTES + 1];
static uint8_t enabled;
static unsigned drops;
static bool device_busy;
static unsigned completed, failed, device_key;
static char device_result[48] = "idle";

bool macro_json_safe(const char *raw, size_t length) {
    unsigned slashes = 0;
    for (size_t i = 0; i < length; ++i) {
        if (!raw[i]) return false;
        if (raw[i] == '\\') { ++slashes; continue; }
        if ((slashes & 1) && raw[i] == 'u' && i + 4 < length && !memcmp(raw + i + 1, "0000", 4)) return false;
        slashes = 0;
    }
    return true;
}

static bool fields(const cJSON *root, const char *const *names, unsigned count) {
    if (!cJSON_IsObject(root) || (unsigned)cJSON_GetArraySize(root) != count) return false;
    for (const cJSON *item = root->child; item; item = item->next) {
        bool found = false;
        for (unsigned i = 0; i < count; ++i) if (item->string && !strcmp(item->string, names[i])) found = true;
        if (!found) return false;
        for (const cJSON *other = item->next; other; other = other->next)
            if (other->string && !strcmp(item->string, other->string)) return false;
    }
    return true;
}
static bool integer(const cJSON *v, unsigned maximum) {
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= 0 &&
        v->valuedouble <= maximum && floor(v->valuedouble) == v->valuedouble;
}
static bool utf8(const char *s, size_t maximum) {
    if (!s || strlen(s) > maximum) return false;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned c = *p++, extra = 0, minimum = 0;
        if (c < 0x80) { if ((c < 32 && c != 9 && c != 10) || c == 127) return false; continue; }
        if (c >= 0xc2 && c <= 0xdf) { extra = 1; minimum = 0x80; c &= 31; }
        else if (c >= 0xe0 && c <= 0xef) { extra = 2; minimum = 0x800; c &= 15; }
        else if (c >= 0xf0 && c <= 0xf4) { extra = 3; minimum = 0x10000; c &= 7; }
        else return false;
        for (unsigned i = 0; i < extra; ++i) { if ((*p & 0xc0) != 0x80) return false; c = (c << 6) | (*p++ & 63); }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}
static bool key_name(const char *s) {
    if (!s) return false;
    if (strlen(s) == 1 && ((*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || strchr("`-=[]\\;',./", *s))) return true;
    char expected[5]; for (unsigned n = 1; n <= 24; ++n) { snprintf(expected, sizeof(expected), "F%u", n); if (!strcmp(s, expected)) return true; }
    const char *const names[] = {"CTRL","SHIFT","ALT","META","RCTRL","RSHIFT","RALT","RMETA","ENTER","ESC","BACKSPACE","TAB","SPACE","CAPSLOCK","INSERT","HOME","PAGEUP","DELETE","END","PAGEDOWN","LEFT","RIGHT","UP","DOWN","PRINTSCREEN","SCROLLLOCK","PAUSE","NUMLOCK","VOLUMEUP","VOLUMEDOWN","MUTE","PLAYPAUSE","NEXT","PREVIOUS"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) if (!strcmp(s, names[i])) return true;
    return false;
}
bool macro_program_valid(const cJSON *p) {
    const char *const top[] = {"name","steps"}, *const extended[] = {"name","steps","text_mode"};
    const cJSON *mode=cJSON_GetObjectItemCaseSensitive(p,"text_mode");
    if (!fields(p, top, 2) && !fields(p, extended, 3)) return false;
    if(mode && (!cJSON_IsString(mode) || (strcmp(mode->valuestring,"ascii") && strcmp(mode->valuestring,"linux") && strcmp(mode->valuestring,"macos"))))return false;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(p, "name"), *steps = cJSON_GetObjectItemCaseSensitive(p, "steps");
    if (!cJSON_IsString(name) || !utf8(name->valuestring, 64) || !cJSON_IsArray(steps) || cJSON_GetArraySize(steps) > MACRO_STEPS) return false;
    unsigned delay = 0;
    for (const cJSON *step = steps->child; step; step = step->next) {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(step, "type");
        if (!cJSON_IsString(type)) return false;
        if (!strcmp(type->valuestring, "keys")) {
            const char *const names[] = {"type","keys"};
            const cJSON *keys = cJSON_GetObjectItemCaseSensitive(step, "keys");
            if (!fields(step, names, 2) || !cJSON_IsArray(keys) || !cJSON_GetArraySize(keys) || cJSON_GetArraySize(keys) > MACRO_CHORD_KEYS) return false;
            for (const cJSON *key = keys->child; key; key = key->next) {
                if (!cJSON_IsString(key) || !key_name(key->valuestring)) return false;
                for (const cJSON *other = key->next; other; other = other->next)
                    if (cJSON_IsString(other) && !strcmp(key->valuestring, other->valuestring)) return false;
            }
        } else if (!strcmp(type->valuestring, "text")) {
            const char *const names[] = {"type","text"}; const cJSON *text = cJSON_GetObjectItemCaseSensitive(step, "text");
            if (!fields(step, names, 2) || !cJSON_IsString(text) || !*text->valuestring || !utf8(text->valuestring, 512)) return false;
        } else if (!strcmp(type->valuestring, "wait")) {
            const char *const names[] = {"type","ms"}; const cJSON *ms = cJSON_GetObjectItemCaseSensitive(step, "ms");
            if (!fields(step, names, 2) || !integer(ms, 5000)) return false;
            delay += (unsigned)ms->valuedouble;
        } else return false;
    }
    if (delay > 30000) return false;
    char *raw = cJSON_PrintUnformatted(p); if (!raw) return false;
    bool valid = strlen(raw) <= MACRO_PROGRAM_BYTES; free(raw); return valid;
}
esp_err_t macro_store_init(void) {
    lock = xSemaphoreCreateMutex(); storage_lock = xSemaphoreCreateMutex(); if (!lock || !storage_lock) return ESP_ERR_NO_MEM;
    nvs_handle_t handle;
    if (nvs_open("pcb2_macros", NVS_READONLY, &handle) == ESP_OK) {
        for (unsigned i = 0; i < 4; ++i) {
            char key[8]; snprintf(key, sizeof(key), "key%u", i); size_t size = sizeof(programs[i]);
            if (nvs_get_str(handle, key, programs[i], &size) == ESP_OK) {
                cJSON *p = cJSON_Parse(programs[i]);
                if (macro_program_valid(p) && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(p, "steps"))) enabled |= 1U << i;
                else *programs[i] = 0;
                cJSON_Delete(p);
            }
        }
        nvs_close(handle);
    }
    return ESP_OK;
}
cJSON *macro_store_get(unsigned key) {
    if (key >= 4) return NULL;
    xSemaphoreTake(lock, portMAX_DELAY);
    cJSON *p = cJSON_Parse(*programs[key] ? programs[key] : "{\"name\":\"\",\"steps\":[]}");
    xSemaphoreGive(lock); return p;
}
uint8_t macro_store_mask(void) { xSemaphoreTake(lock, portMAX_DELAY); uint8_t result = enabled; xSemaphoreGive(lock); return result; }
cJSON *macro_store_status(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    cJSON *p = cJSON_CreateObject();
    if (p) {
        cJSON_AddNumberToObject(p,"version",1);cJSON_AddStringToObject(p,"execution","device");
        cJSON_AddNumberToObject(p,"enabled",enabled);cJSON_AddBoolToObject(p,"host_connected",false);
        cJSON_AddBoolToObject(p,"busy",device_busy);cJSON_AddNumberToObject(p,"dropped",drops);
        cJSON_AddNumberToObject(p,"completed",completed);cJSON_AddNumberToObject(p,"failed",failed);
        cJSON_AddNumberToObject(p,"last_key",device_key+1);cJSON_AddStringToObject(p,"result",device_result);
    }
    xSemaphoreGive(lock); return p;
}
void macro_store_execution(bool busy,unsigned key,const char *result) {
    xSemaphoreTake(lock,portMAX_DELAY);device_busy=busy;
    if(!strcmp(result,"busy"))++drops;
    else {device_key=key;snprintf(device_result,sizeof(device_result),"%s",result);if(!strcmp(result,"completed"))++completed;
        else if(!busy && strcmp(result,"idle"))++failed;}
    xSemaphoreGive(lock);
}
// Retained protocol endpoints are inert: v10 never delegates physical input to a host.
void macro_store_press(unsigned key) { (void)key; }
cJSON *macro_store_event(void) { return cJSON_CreateObject(); }
esp_err_t macro_store_command(const char *path, cJSON *body) {
    if (!strcmp(path, "/api/macro")) {
        const char *const names[] = {"key","program"}; const cJSON *key = cJSON_GetObjectItemCaseSensitive(body, "key"), *p = cJSON_GetObjectItemCaseSensitive(body, "program");
        if (!fields(body, names, 2) || !integer(key, 3) || !macro_program_valid(p)) return ESP_ERR_INVALID_ARG;
        char *raw = cJSON_PrintUnformatted(p); if (!raw) return ESP_ERR_NO_MEM;
        char name[8]; snprintf(name, sizeof(name), "key%u", (unsigned)key->valuedouble);
        xSemaphoreTake(storage_lock, portMAX_DELAY); nvs_handle_t handle; esp_err_t err = nvs_open("pcb2_macros", NVS_READWRITE, &handle);
        if (err == ESP_OK) { err = nvs_set_str(handle, name, raw); if (err == ESP_OK) err = nvs_commit(handle); nvs_close(handle); }
        if (err == ESP_OK) { xSemaphoreTake(lock, portMAX_DELAY); unsigned i = (unsigned)key->valuedouble; strcpy(programs[i], raw); if (cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(p, "steps"))) enabled |= 1U << i; else enabled &= ~(1U << i); xSemaphoreGive(lock); }
        xSemaphoreGive(storage_lock); free(raw); return err;
    }
    // Old companion tools cannot acquire an execution lease or duplicate device input.
    return ESP_ERR_INVALID_STATE;
}
