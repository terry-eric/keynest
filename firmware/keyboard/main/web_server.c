#include "app.h"
#include "app_api.h"
#include "recorder.h"
#include "mic_monitor.h"
#include "key_sound.h"
#include "device_control.h"
#include "macro_store.h"
#include "mbedtls/base64.h"
#include "pcb2_board.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "cJSON.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <math.h>
#include <inttypes.h>

extern const unsigned char html_start[] asm("_binary_index_html_start"), html_end[] asm("_binary_index_html_end");
extern const unsigned char js_start[] asm("_binary_app_js_start"), js_end[] asm("_binary_app_js_end");
extern const unsigned char css_start[] asm("_binary_style_css_start"), css_end[] asm("_binary_style_css_end");
static char ap_name[33];

static esp_err_t json_response(httpd_req_t *req, cJSON *json) {
    if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    char *text = cJSON_PrintUnformatted(json); cJSON_Delete(json);
    if (!text) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json"); httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN); free(text); return err;
}
static esp_err_t json_error(httpd_req_t *req, const char *status_code, const char *message) {
    httpd_resp_set_status(req, status_code);
    cJSON *json = cJSON_CreateObject();
    if (json) cJSON_AddStringToObject(json, "error", message);
    return json_response(req, json);
}
static esp_err_t operation_response(httpd_req_t *req, esp_err_t err) {
    if (err != ESP_OK) return json_error(req, err == ESP_ERR_INVALID_STATE ? "409 Conflict" : "503 Service Unavailable", esp_err_to_name(err));
    cJSON *json = cJSON_CreateObject(); if (json) cJSON_AddBoolToObject(json, "ok", true);
    return json_response(req, json);
}
static void add_bytes(cJSON *object, const char *name, const uint8_t *bytes, unsigned count) {
    cJSON *array = cJSON_AddArrayToObject(object, name);
    if (array) for (unsigned i = 0; i < count; ++i) cJSON_AddItemToArray(array, cJSON_CreateNumber(bytes[i]));
}
static cJSON *config_json(const keypad_config_t *c) {
    cJSON *json = cJSON_CreateObject(); if (!json) return NULL;
    cJSON_AddNumberToObject(json, "led_mode", c->led_mode); cJSON_AddNumberToObject(json, "brightness", c->brightness);
    cJSON_AddNumberToObject(json, "speed", c->speed); cJSON_AddNumberToObject(json, "volume", c->volume);
    cJSON_AddNumberToObject(json, "mic_gain", c->mic_gain); cJSON_AddNumberToObject(json, "sound", c->sound);
    add_bytes(json, "color", c->color, 3); add_bytes(json, "keycodes", c->keycodes, 4); add_bytes(json, "modifiers", c->modifiers, 4);
    return json;
}
cJSON *app_state_json(void) {
    app_snapshot_t state; app_snapshot(&state);
    recorder_status_t recording; recorder_snapshot(&recording);
    cJSON *json = cJSON_CreateObject(); if (!json) return NULL;
    cJSON_AddStringToObject(json, "device", "PCB2 Keypad"); cJSON_AddStringToObject(json, "ssid", ap_name);
    cJSON_AddStringToObject(json, "ip", "192.168.4.1"); cJSON_AddNumberToObject(json, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddBoolToObject(json, "usb_connected", usb_keyboard_connected()); cJSON_AddNumberToObject(json, "keys", state.keys);
    cJSON_AddBoolToObject(json, "dirty", state.dirty); cJSON_AddBoolToObject(json, "audio_ready", state.audio_ready);
    cJSON_AddNumberToObject(json, "mic_dbfs", state.mic_dbfs); cJSON_AddNumberToObject(json, "mic_peak", state.mic_peak);
    cJSON_AddBoolToObject(json, "mic_preview", true);
    cJSON_AddBoolToObject(json, "usb_auto_download", true);
    cJSON_AddItemToObject(json, "macros", macro_store_status());
    cJSON_AddNumberToObject(json,"psram_bytes",heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(json,"psram_free_bytes",heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddNumberToObject(json,"internal_free_bytes",heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
#if CONFIG_PCB2_MIC_ZTS6672
    cJSON_AddStringToObject(json, "mic_model", "ZTS6672");
#else
    cJSON_AddStringToObject(json, "mic_model", "ICS-43434");
#endif
    cJSON_AddStringToObject(json, "mic_slot", pcb2_audio_mic_right() ? "right" : "left");
    pcb2_audio_diagnostics_t slots; pcb2_audio_get_diagnostics(&slots);
    cJSON *slot_array = cJSON_AddArrayToObject(json, "mic_slots");
    if (slot_array && slots.frames) for (unsigned ch = 0; ch < 2; ++ch) {
        double mean = (double)slots.sum[ch] / slots.frames;
        double variance = fmax(0, (double)slots.squares[ch] / slots.frames - mean * mean);
        cJSON *slot = cJSON_CreateObject(); if (!slot) continue;
        cJSON_AddStringToObject(slot, "slot", ch ? "right" : "left");
        cJSON_AddNumberToObject(slot, "frames", slots.frames);
        cJSON_AddNumberToObject(slot, "min", slots.min[ch]);
        cJSON_AddNumberToObject(slot, "max", slots.max[ch]);
        cJSON_AddNumberToObject(slot, "dc_pcm24", mean);
        cJSON_AddNumberToObject(slot, "ac_dbfs", 20 * log10(fmax(sqrt(variance) / 8388608, 0.000001)));
        cJSON_AddItemToArray(slot_array, slot);
    }
    cJSON_AddNumberToObject(json, "queue_drops", state.queue_drops);
    cJSON_AddItemToObject(json, "config", config_json(&state.config));
    cJSON *rec = cJSON_AddObjectToObject(json, "recording");
    if (rec) {
        cJSON_AddBoolToObject(rec, "sd_ready", recording.sd_ready); cJSON_AddBoolToObject(rec, "mounting", recording.mounting);
        cJSON_AddBoolToObject(rec, "flash_ready", recording.flash_ready);
        cJSON_AddNumberToObject(rec, "target", recording.target); cJSON_AddNumberToObject(rec, "last_target", recording.last_target);
        cJSON_AddBoolToObject(rec, "downloading", recording.downloading); cJSON_AddNumberToObject(rec, "state", recording.state);
        cJSON_AddNumberToObject(rec, "frames", recording.frames);
        cJSON_AddNumberToObject(rec, "duration_ms", (double)recording.frames * 1000 / CAPTURE_RATE);
        cJSON_AddNumberToObject(rec, "dropped_frames", recording.dropped_frames);
        cJSON_AddNumberToObject(rec, "limit_seconds", CONFIG_PCB2_RECORD_SECONDS);
        cJSON_AddNumberToObject(rec, "flash_limit_seconds", (double)FLASH_CAPTURE_FRAMES / CAPTURE_RATE);
        cJSON_AddStringToObject(rec, "filename", recording.filename); cJSON_AddStringToObject(rec, "error", recording.error);
    }
    key_sound_status_t sample; key_sound_snapshot(&sample);
    cJSON *custom = cJSON_AddObjectToObject(json, "custom_sound");
    if (custom) {
        cJSON_AddBoolToObject(custom,"supported",true); cJSON_AddBoolToObject(custom,"ready",sample.ready);
        cJSON_AddBoolToObject(custom,"uploading",sample.uploading);
        cJSON_AddStringToObject(custom,"storage",sample.storage==REC_SD?"sd":"flash");
        cJSON_AddStringToObject(custom,"filename",sample.filename);
        cJSON_AddNumberToObject(custom,"frames",sample.frames); cJSON_AddNumberToObject(custom,"crc32",sample.crc32);
        cJSON_AddNumberToObject(custom,"max_seconds",2);
        cJSON_AddBoolToObject(custom,"per_key",sample.per_key);
        cJSON *sounds=cJSON_AddArrayToObject(custom,"keys");
        for(unsigned i=0;sounds && i<4;++i) {
            const key_sound_clip_t *clip=&sample.keys[i];cJSON *item=cJSON_CreateObject();
            cJSON_AddBoolToObject(item,"ready",clip->ready);cJSON_AddBoolToObject(item,"custom",clip->custom);
            cJSON_AddStringToObject(item,"storage",clip->storage==REC_SD?"sd":"flash");
            cJSON_AddStringToObject(item,"filename",clip->filename);cJSON_AddStringToObject(item,"error",clip->error);
            cJSON_AddNumberToObject(item,"frames",clip->frames);cJSON_AddNumberToObject(item,"crc32",clip->crc32);
            cJSON_AddItemToArray(sounds,item);
        }
        cJSON_AddStringToObject(custom,"error",sample.error);
        cJSON *plays=cJSON_AddArrayToObject(custom,"plays"), *rendered=cJSON_AddArrayToObject(custom,"rendered");
        for(unsigned i=0;i<4;++i) {
            if(plays)cJSON_AddItemToArray(plays,cJSON_CreateNumber(sample.plays[i]));
            if(rendered)cJSON_AddItemToArray(rendered,cJSON_CreateNumber(sample.rendered[i]));
        }
    }
    return json;
}
cJSON *app_sd_files_json(void) {
    recorder_sd_file_t files[32];size_t count=0;bool truncated=false;
    esp_err_t err=recorder_list_sd(files,32,&count,&truncated);
    cJSON *json=cJSON_CreateObject();if(!json)return NULL;
    cJSON_AddBoolToObject(json,"ok",err==ESP_OK);
    if(err!=ESP_OK)cJSON_AddStringToObject(json,"error","SD unavailable or busy");
    cJSON_AddBoolToObject(json,"truncated",truncated);
    cJSON *array=cJSON_AddArrayToObject(json,"files");
    for(size_t i=0;array && i<count;++i) {
        cJSON *item=cJSON_CreateObject();if(!item)continue;
        cJSON_AddStringToObject(item,"name",files[i].name);cJSON_AddNumberToObject(item,"bytes",files[i].bytes);
        cJSON_AddItemToArray(array,item);
    }
    return json;
}
static esp_err_t sd_files_get(httpd_req_t *req) {return json_response(req,app_sd_files_json());}
static esp_err_t state_get(httpd_req_t *req) {
    return json_response(req, app_state_json());
}
static esp_err_t macro_get(httpd_req_t *req) {
    if (!strcmp(req->uri, "/api/macro/event")) return json_response(req, macro_store_event());
    size_t n = strlen(req->uri);
    if (n != 12 || req->uri[11] < '0' || req->uri[11] > '3') return json_error(req, "400 Bad Request", "Invalid key");
    return json_response(req, macro_store_get(req->uri[11] - '0'));
}
static bool fields_match(cJSON *root, const char *const *allowed, unsigned count) {
    if (!cJSON_IsObject(root)) return false;
    for (cJSON *item = root->child; item; item = item->next) {
        bool found = false;
        for (unsigned i = 0; i < count; ++i) if (item->string && !strcmp(item->string, allowed[i])) found = true;
        if (!found) return false;
        for (cJSON *other = item->next; other; other = other->next)
            if (other->string && !strcmp(item->string, other->string)) return false;
    }
    return true;
}
static bool number(cJSON *item, unsigned min, unsigned max, uint8_t *result) {
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < min || item->valuedouble > max ||
        floor(item->valuedouble) != item->valuedouble) return false;
    *result = item->valuedouble; return true;
}
static bool byte_array(cJSON *root, const char *name, uint8_t *out, unsigned count) {
    cJSON *array = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != count) return false;
    for (unsigned i = 0; i < count; ++i) if (!number(cJSON_GetArrayItem(array, i), 0, 255, &out[i])) return false;
    return true;
}
static bool parse_config(cJSON *json, keypad_config_t *out) {
    static const char *const names[] = {"led_mode","brightness","speed","volume","mic_gain","sound","color","keycodes","modifiers"};
    if (!fields_match(json, names, 9)) return false;
    memset(out, 0, sizeof(*out)); out->schema = 1;
    return number(cJSON_GetObjectItemCaseSensitive(json, "led_mode"), 0, LED_MODE_COUNT - 1, &out->led_mode) &&
        number(cJSON_GetObjectItemCaseSensitive(json, "brightness"), 0, 255, &out->brightness) &&
        number(cJSON_GetObjectItemCaseSensitive(json, "speed"), 20, 200, &out->speed) &&
        number(cJSON_GetObjectItemCaseSensitive(json, "volume"), 0, 100, &out->volume) &&
        number(cJSON_GetObjectItemCaseSensitive(json, "mic_gain"), 1, 16, &out->mic_gain) &&
        number(cJSON_GetObjectItemCaseSensitive(json, "sound"), 0, SOUND_COUNT - 1, &out->sound) &&
        byte_array(json, "color", out->color, 3) && byte_array(json, "keycodes", out->keycodes, 4) &&
        byte_array(json, "modifiers", out->modifiers, 4) && logic_config_valid(out);
}
static cJSON *read_body(httpd_req_t *req) {
    char control[8] = {0}, content_type[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-PCB2-Control", control, sizeof(control)) != ESP_OK || strcmp(control, "1")) {
        json_error(req, "403 Forbidden", "Missing control header"); return NULL;
    }
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strncasecmp(content_type, "application/json", 16) || (content_type[16] && content_type[16] != ';')) {
        json_error(req, "415 Unsupported Media Type", "Use application/json"); return NULL;
    }
    if (req->content_len == 0 || req->content_len > 1536) {
        json_error(req, "413 Content Too Large", "JSON body must be 1..1536 bytes"); return NULL;
    }
    char *body = malloc(req->content_len + 1);
    if (!body) { json_error(req, "503 Service Unavailable", "Out of memory"); return NULL; }
    size_t received = 0;
    while (received < req->content_len) {
        int result = httpd_req_recv(req, body + received, req->content_len - received);
        if (result <= 0) { free(body); json_error(req, "408 Request Timeout", "Incomplete request"); return NULL; }
        received += result;
    }
    body[received] = 0;
    cJSON *json = macro_json_safe(body, received) ? cJSON_ParseWithLengthOpts(body, received + 1, NULL, true) : NULL;
    free(body);
    if (!cJSON_IsObject(json)) { cJSON_Delete(json); json_error(req, "400 Bad Request", "Invalid JSON object"); return NULL; }
    return json;
}
static bool uint32_field(cJSON *body,const char *name,uint32_t *out) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(body,name);
    if(!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<0 ||
       v->valuedouble>UINT32_MAX || floor(v->valuedouble)!=v->valuedouble) return false;
    *out=(uint32_t)v->valuedouble; return true;
}
static esp_err_t sound_command(const char *path,cJSON *body) {
    if(!strcmp(path,"/api/sound/default")) {
        const char *const fields[]={"key"};uint32_t key;
        if(!fields_match(body,fields,1) || !uint32_field(body,"key",&key) || key>3)return ESP_ERR_INVALID_ARG;
        return key_sound_use_default(key);
    }
    cJSON *owner=cJSON_GetObjectItemCaseSensitive(body,"owner");
    if(!cJSON_IsString(owner) || strlen(owner->valuestring)!=16) return ESP_ERR_INVALID_ARG;
    if(!strcmp(path,"/api/sound/begin")) {
        const char *const fields[]={"owner","storage","frames","crc32","key"};
        cJSON *storage=cJSON_GetObjectItemCaseSensitive(body,"storage"); uint32_t frames,crc;
        cJSON *key_item=cJSON_GetObjectItemCaseSensitive(body,"key");uint32_t key=0;int selected=-1;
        if(key_item){if(!uint32_field(body,"key",&key)||key>3)return ESP_ERR_INVALID_ARG;selected=(int)key;}
        if(!fields_match(body,fields,5) || !cJSON_IsString(storage) ||
           !uint32_field(body,"frames",&frames) || !uint32_field(body,"crc32",&crc)) return ESP_ERR_INVALID_ARG;
        unsigned target=!strcmp(storage->valuestring,"flash")?REC_FLASH:!strcmp(storage->valuestring,"sd")?REC_SD:99;
        return key_sound_begin(owner->valuestring,target,frames,crc,selected);
    }
    if(!strcmp(path,"/api/sound/chunk")) {
        const char *const fields[]={"owner","offset","pcm","crc32"};
        cJSON *pcm=cJSON_GetObjectItemCaseSensitive(body,"pcm"); uint32_t offset,crc; uint8_t bytes[1024]; size_t count=0;
        if(!fields_match(body,fields,4) || !cJSON_IsString(pcm) || strlen(pcm->valuestring)>1368 ||
           !uint32_field(body,"offset",&offset) || !uint32_field(body,"crc32",&crc) ||
           mbedtls_base64_decode(bytes,sizeof(bytes),&count,(const unsigned char*)pcm->valuestring,strlen(pcm->valuestring)))
            return ESP_ERR_INVALID_ARG;
        return key_sound_chunk(owner->valuestring,offset,bytes,count,crc);
    }
    const char *const fields[]={"owner"};
    if(!fields_match(body,fields,1)) return ESP_ERR_INVALID_ARG;
    if(!strcmp(path,"/api/sound/commit")) return key_sound_commit(owner->valuestring);
    if(!strcmp(path,"/api/sound/cancel")) return key_sound_cancel(owner->valuestring);
    return ESP_ERR_INVALID_ARG;
}
esp_err_t app_command_execute(const char *path, cJSON *body) {
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (!strcmp(path,"/api/macro")) {
        recorder_status_t rec; recorder_snapshot(&rec);
        if (rec.state == REC_STARTING || rec.state == REC_ACTIVE || rec.state == REC_STOPPING || rec.downloading) return ESP_ERR_INVALID_STATE;
        return macro_store_command(path,body);
    }
    if (!strncmp(path,"/api/macro/",11)) return macro_store_command(path,body);
    if(!strcmp(path,"/api/device/restart")) {
        const char *const fields[]={"mode"};cJSON *mode=cJSON_GetObjectItemCaseSensitive(body,"mode");
        if(!fields_match(body,fields,1) || !cJSON_IsString(mode) ||
           (strcmp(mode->valuestring,"bootloader") && strcmp(mode->valuestring,"run")))return ESP_ERR_INVALID_ARG;
        return device_request_restart(!strcmp(mode->valuestring,"bootloader"));
    }
    if (!strncmp(path,"/api/sound/",11)) return sound_command(path,body);
    if (!strcmp(path, "/api/config")) {
        keypad_config_t config;
        if (parse_config(body, &config)) result = app_apply_config(&config);
    } else if (!strcmp(path, "/api/trigger")) {
        const char *const fields[] = {"key"}; uint8_t key;
        if (fields_match(body, fields, 1) && number(cJSON_GetObjectItemCaseSensitive(body, "key"), 0, 3, &key)) result = app_trigger_key(key);
    } else if (!strcmp(path, "/api/record")) {
        const char *const fields[] = {"action", "target", "owner"}; cJSON *action = cJSON_GetObjectItemCaseSensitive(body, "action");
        if (fields_match(body, fields, 3) && cJSON_IsString(action)) {
            if (!strcmp(action->valuestring, "stop")) result = recorder_stop();
            if (!strcmp(action->valuestring, "start")) {
                app_snapshot_t state; app_snapshot(&state);
                cJSON *target = cJSON_GetObjectItemCaseSensitive(body, "target"), *owner = cJSON_GetObjectItemCaseSensitive(body, "owner");
                if (cJSON_IsString(target) && cJSON_IsString(owner)) {
                    unsigned destination = !strcmp(target->valuestring, "browser") ? REC_BROWSER :
                        !strcmp(target->valuestring, "flash") ? REC_FLASH : !strcmp(target->valuestring, "sd") ? REC_SD : 99;
                    result = state.audio_ready ? recorder_begin(destination, owner->valuestring) : ESP_ERR_INVALID_STATE;
                }
            }
        }
    } else if (!strcmp(path, "/api/microphone/channel")) {
        const char *const fields[] = {"slot"};
        cJSON *slot = cJSON_GetObjectItemCaseSensitive(body, "slot");
        if (fields_match(body, fields, 1) && cJSON_IsString(slot) &&
            (!strcmp(slot->valuestring, "left") || !strcmp(slot->valuestring, "right"))) {
            recorder_status_t recording; recorder_snapshot(&recording);
            if (recording.state) result = ESP_ERR_INVALID_STATE;
            else { pcb2_audio_set_mic_right(!strcmp(slot->valuestring, "right")); result = ESP_OK; }
        }
    } else if (!strcmp(path, "/api/save") && fields_match(body, NULL, 0)) result = app_save_config();
    else if (!strcmp(path, "/api/sd") && fields_match(body, NULL, 0)) result = recorder_retry_mount();
    return result;
}
static esp_err_t command_post(httpd_req_t *req) {
    cJSON *body = read_body(req); if (!body) return ESP_FAIL; // Close unread/malformed request bodies.
    esp_err_t result = app_command_execute(req->uri, body);
    cJSON_Delete(body);
    if (result == ESP_ERR_INVALID_ARG) return json_error(req, "400 Bad Request", "Invalid or unknown fields");
    return operation_response(req, result);
}
static esp_err_t file_get(httpd_req_t *req) {
    char filename[16];esp_err_t err;
    if(!strcmp(req->uri,"/api/recording.wav"))err=recorder_download_begin(filename);
    else {
        char query[64];
        if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK ||
           httpd_query_key_value(query,"name",filename,sizeof(filename))!=ESP_OK)
            return json_error(req,"400 Bad Request","Missing SD audio filename");
        err=recorder_download_sd_begin(filename);
    }
    if (err != ESP_OK) return json_error(req, "409 Conflict", "Finish recording before downloading");
    char disposition[80]; snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_type(req, "audio/wav"); httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char buffer[2048]; size_t bytes, offset = 0;
    while ((err = recorder_download_read(offset, buffer, sizeof(buffer), &bytes)) == ESP_OK && bytes) {
        err = httpd_resp_send_chunk(req, buffer, bytes); if (err != ESP_OK) break;
        offset += bytes;
    }
    recorder_download_end();
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}
static esp_err_t audio_get(httpd_req_t *req) {
    char owner[17] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-PCB2-Recording", owner, sizeof(owner)) != ESP_OK)
        return json_error(req, "403 Forbidden", "Missing recording owner");
    int16_t buffer[1024]; size_t count = 0;
    esp_err_t err = recorder_stream_read(owner, buffer, 1024, &count);
    if (err != ESP_OK) return json_error(req, "409 Conflict", "Recording belongs to another session");
    recorder_status_t status; recorder_snapshot(&status);
    char state[12], frames[16], dropped[16];
    snprintf(state, sizeof(state), "%u", status.state); snprintf(frames, sizeof(frames), "%lu", (unsigned long)status.frames);
    snprintf(dropped, sizeof(dropped), "%lu", (unsigned long)status.dropped_frames);
    httpd_resp_set_type(req, "application/octet-stream"); httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-PCB2-State", state); httpd_resp_set_hdr(req, "X-PCB2-Frames", frames);
    httpd_resp_set_hdr(req, "X-PCB2-Dropped", dropped);
    return httpd_resp_send(req, (const char *)buffer, count * sizeof(int16_t));
}
static esp_err_t microphone_get(httpd_req_t *req) {
    int16_t buffer[MIC_MONITOR_FRAMES]; mic_monitor_snapshot_t snapshot;
    mic_monitor_read(buffer, MIC_MONITOR_FRAMES, &snapshot);
    int64_t age_ms = (esp_timer_get_time() - snapshot.captured_us) / 1000;
    if (!snapshot.count || age_ms > 1000)
        return json_error(req, "503 Service Unavailable", "Microphone samples are not ready");
    char frames[24], age[24];
    snprintf(frames, sizeof(frames), "%" PRIu64, snapshot.frames);
    snprintf(age, sizeof(age), "%" PRId64, age_ms);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-PCB2-Sample-Rate", "16000");
    httpd_resp_set_hdr(req, "X-PCB2-Frames", frames);
    httpd_resp_set_hdr(req, "X-PCB2-Age-Ms", age);
    return httpd_resp_send(req, (const char *)buffer, snapshot.count * sizeof(int16_t));
}
static esp_err_t asset_get(httpd_req_t *req) {
    const unsigned char *start = html_start, *end = html_end; const char *type = "text/html; charset=utf-8";
    if (!strcmp(req->uri, "/app.js")) { start = js_start; end = js_end; type = "text/javascript; charset=utf-8"; }
    if (!strcmp(req->uri, "/style.css")) { start = css_start; end = css_end; type = "text/css; charset=utf-8"; }
    httpd_resp_set_type(req, type); httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; media-src 'self' blob:; object-src 'none'; frame-ancestors 'none'; base-uri 'none'");
    return httpd_resp_send(req, (const char *)start, end - start - 1);
}
esp_err_t web_server_start(void) {
    if (strlen(CONFIG_PCB2_AP_PREFIX) > 26 || strlen(CONFIG_PCB2_AP_PASSWORD) < 8 || strlen(CONFIG_PCB2_AP_PASSWORD) > 63) return ESP_ERR_INVALID_ARG;
    esp_err_t err = esp_netif_init(); if (err != ESP_OK) return err;
    err = esp_event_loop_create_default(); if (err != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_ap()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT(); err = esp_wifi_init(&init); if (err != ESP_OK) return err;
    uint8_t mac[6]; err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP); if (err != ESP_OK) return err;
    snprintf(ap_name, sizeof(ap_name), "%s-%02X%02X", CONFIG_PCB2_AP_PREFIX, mac[4], mac[5]);
    wifi_config_t wifi = {.ap = {.channel = 6, .max_connection = 4, .authmode = WIFI_AUTH_WPA2_PSK}};
    wifi.ap.ssid_len = strlen(ap_name);
    memcpy(wifi.ap.ssid, ap_name, wifi.ap.ssid_len);
    snprintf((char *)wifi.ap.password, sizeof(wifi.ap.password), "%s", CONFIG_PCB2_AP_PASSWORD);
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &wifi);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) return err;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); config.stack_size = 8192; config.max_uri_handlers = 32;
    config.max_open_sockets = 6; config.lru_purge_enable = true; config.recv_wait_timeout = 3; config.send_wait_timeout = 3;
    httpd_handle_t server; err = httpd_start(&server, &config); if (err != ESP_OK) return err;
    const char *assets[] = {"/", "/app.js", "/style.css"};
    for (unsigned i = 0; i < 3; ++i) {
        httpd_uri_t uri = {.uri = assets[i], .method = HTTP_GET, .handler = asset_get};
        err = httpd_register_uri_handler(server, &uri); if (err != ESP_OK) return err;
    }
    httpd_uri_t state = {.uri = "/api/state", .method = HTTP_GET, .handler = state_get};
    err = httpd_register_uri_handler(server, &state); if (err != ESP_OK) return err;
    const char *macro_paths[] = {"/api/macro/0","/api/macro/1","/api/macro/2","/api/macro/3","/api/macro/event"};
    for (unsigned i = 0; i < 5; ++i) { httpd_uri_t uri = {.uri = macro_paths[i], .method = HTTP_GET, .handler = macro_get}; err = httpd_register_uri_handler(server, &uri); if (err != ESP_OK) return err; }
    httpd_uri_t file = {.uri = "/api/recording.wav", .method = HTTP_GET, .handler = file_get};
    err = httpd_register_uri_handler(server, &file); if (err != ESP_OK) return err;
    httpd_uri_t sd_files={.uri="/api/sd/files",.method=HTTP_GET,.handler=sd_files_get};
    err=httpd_register_uri_handler(server,&sd_files);if(err!=ESP_OK)return err;
    httpd_uri_t sd_file={.uri="/api/sd/recording.wav",.method=HTTP_GET,.handler=file_get};
    err=httpd_register_uri_handler(server,&sd_file);if(err!=ESP_OK)return err;
    httpd_uri_t audio = {.uri = "/api/audio", .method = HTTP_GET, .handler = audio_get};
    err = httpd_register_uri_handler(server, &audio); if (err != ESP_OK) return err;
    httpd_uri_t microphone = {.uri = "/api/microphone", .method = HTTP_GET, .handler = microphone_get};
    err = httpd_register_uri_handler(server, &microphone); if (err != ESP_OK) return err;
    const char *commands[] = {"/api/config", "/api/trigger", "/api/record", "/api/save", "/api/sd", "/api/microphone/channel", "/api/sound/begin", "/api/sound/chunk", "/api/sound/commit", "/api/sound/cancel", "/api/sound/default", "/api/device/restart", "/api/macro", "/api/macro/host", "/api/macro/ack"};
    for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        httpd_uri_t uri = {.uri = commands[i], .method = HTTP_POST, .handler = command_post};
        err = httpd_register_uri_handler(server, &uri); if (err != ESP_OK) return err;
    }
    ESP_LOGI("web", "Connect to %s, then open http://192.168.4.1", ap_name);
    return ESP_OK;
}
