#include "app.h"
#include "app_api.h"
#include "macro_store.h"
#include "macro_engine.h"
#include "key_gestures.h"
#include "device_control.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

_Static_assert(sizeof(macro_frame_t)==30,"HID frame must contain 29 keyboard bytes and one consumer byte");
_Static_assert(CFG_TUD_HID_EP_BUFSIZE>=30,"TinyUSB HID buffer must fit report ID plus NKRO bitmap");

static const uint8_t report_descriptor[] = {
    // Keyboard report 1: eight modifiers, 224 usage bits (no six-key truncation).
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,
    0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,
    0x19,0x00,0x29,0xdf,0x95,0xe0,0x81,0x02,
    0x05,0x08,0x19,0x01,0x29,0x05,0x95,0x05,0x91,0x02,0x95,0x03,0x91,0x03,0xc0,
    // Consumer report 2: mute, volume up/down, play/pause, next/previous.
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x02,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x06,
    0x09,0xe2,0x09,0xe9,0x09,0xea,0x09,0xcd,0x09,0xb5,0x09,0xb6,0x81,0x02,0x95,0x02,0x81,0x03,0xc0
};
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_HID_DESCRIPTOR(2, 5, HID_ITF_PROTOCOL_NONE, sizeof(report_descriptor), 0x83, 32, 5)
};
static char serial[16];
static const char *strings[] = {(const char[]){0x09, 0x04}, "PCB2", "PCB2 Keypad", serial, "Keypad log", "Four-key keyboard"};
static macro_frame_t last_report;
static portMUX_TYPE report_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t last_valid;
static uint8_t leds;
static macro_engine_t engine;
static cJSON *running;
static unsigned running_key, frame_hold;
static macro_frame_t target;
static uint64_t frame_at, report_at, started;
static bool frame_accepted;
static bool release_pending;

static void finish(const char *result) {
    if(running){cJSON_Delete(running);running=NULL;macro_store_execution(false,running_key,result);}
    if(strcmp(result,"completed"))release_pending=true;
    memset(&target,0,sizeof(target));frame_accepted=false;frame_at=0;frame_hold=30;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { (void)instance; return report_descriptor; }
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t *buffer, uint16_t length) {
    (void)instance;
    if (type != HID_REPORT_TYPE_INPUT || !buffer || (report_id!=1 && report_id!=2)) return 0;
    unsigned size=report_id==1?29:1;if(length>size)length=size;
    portENTER_CRITICAL(&report_lock); memcpy(buffer, report_id==1?(const void*)&last_report:(const void*)&last_report.media, length); portEXIT_CRITICAL(&report_lock);
    return length;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, const uint8_t *buffer, uint16_t size) {
    (void)instance;
    if(report_id==1 && type==HID_REPORT_TYPE_OUTPUT && buffer && size){portENTER_CRITICAL(&report_lock);leds=buffer[0];portEXIT_CRITICAL(&report_lock);}
}
bool usb_keyboard_connected(void) { return tud_mounted(); }
void usb_keyboard_tick(uint8_t down, const keypad_config_t *config) {
    static key_gestures_t gestures;
    static uint32_t last_revision;
    static bool configured;
    uint16_t hold[4],twice[4];uint32_t revision=macro_store_gestures(hold,twice);
    if (!tud_mounted() || tud_suspended()) { if(running)finish("disconnected");last_valid=false;key_gestures_reset(&gestures,down);return; }
    if(configured && revision!=last_revision){key_gestures_reset(&gestures,down);}
    configured=true;last_revision=revision;
    uint8_t macros = macro_store_mask(), events[4];
    uint64_t now = esp_timer_get_time() / 1000;
    key_gestures_tick(&gestures,down,now,hold,twice,events);
    uint8_t manual=down & ~macros;
    if(manual)key_gestures_reset(&gestures,down);
    bool cancelled=false;
    if(running && (manual || now-started>180000)){finish(manual?"interrupted":"timeout");cancelled=true;}
    for(unsigned i=0;i<4;i++)if(events[i] && (macros & (1U<<i))){
        if(running || manual || cancelled || release_pending){macro_store_execution(!!running,i,"busy");continue;}
        cJSON *p=macro_store_get(i);if(!p)continue;
        if(events[i]!=KEY_GESTURE_TAP){
            const cJSON *g=cJSON_GetObjectItemCaseSensitive(p,"gestures");
            cJSON *chosen=cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(g,events[i]==KEY_GESTURE_HOLD?"hold":"double"),true);
            cJSON_Delete(p);p=chosen;if(!cJSON_IsObject(p)){cJSON_Delete(p);continue;}
        }
        if(!cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(p,"steps"))){cJSON_Delete(p);continue;}
        const char *error=macro_engine_start(&engine,p);
        if(error){cJSON_Delete(p);macro_store_execution(false,i,error);continue;}
        running=p;running_key=i;started=now;frame_accepted=true;frame_at=now;frame_hold=0;
        macro_store_execution(true,i,"running");
    }
    if(running && frame_accepted && now-frame_at>=frame_hold){
        uint8_t lights;portENTER_CRITICAL(&report_lock);lights=leds;portEXIT_CRITICAL(&report_lock);
        if(!macro_engine_next(&engine,&target,&frame_hold,!!(lights&2)))finish("completed");
        frame_accepted=false;report_at=now;
    }
    if(!running && !cancelled && !release_pending){
        keypad_report_t ordinary;logic_hid_report(manual,config,&ordinary);memset(&target,0,sizeof(target));
        target.modifiers=ordinary.modifiers;for(unsigned i=0;i<6;i++)macro_frame_key(&target,ordinary.keys[i]);
    }
    if(running && !frame_accepted && now-report_at>2000){finish("usb_timeout");cancelled=true;}
    // Each frame's hold begins only after both HID reports are accepted by TinyUSB.
    if(tud_hid_ready()){
        if(!(last_valid&1) || memcmp(&target,&last_report,29)){
            if(tud_hid_report(1,&target,29)){portENTER_CRITICAL(&report_lock);memcpy(&last_report,&target,29);portEXIT_CRITICAL(&report_lock);last_valid|=1;}
            else return;
        }else if(!(last_valid&2) || target.media!=last_report.media){
            if(tud_hid_report(2,&target.media,1)){portENTER_CRITICAL(&report_lock);last_report.media=target.media;portEXIT_CRITICAL(&report_lock);last_valid|=2;}
            else return;
        }else if(!frame_accepted){frame_accepted=true;frame_at=now;release_pending=false;}
    }
}
static uint32_t cdc_baud=115200;
static unsigned reset_sequence;
static int64_t reset_started;
static void cdc_line_coding(int itf,cdcacm_event_t *event) {
    (void)itf;cdc_baud=event->line_coding_changed_data.p_line_coding->bit_rate;
}
static void cdc_line_state(int itf,cdcacm_event_t *event) {
    (void)itf;bool dtr=event->line_state_changed_data.dtr,rts=event->line_state_changed_data.rts;
    if(cdc_baud==1200 && !dtr) {device_request_restart(true);reset_sequence=0;return;}
    unsigned value=(dtr?2:0)|(rts?1:0);
    static const unsigned sequence[]={1,3,2,0};
    int64_t now=esp_timer_get_time();
    if(reset_sequence && now-reset_started>2000000)reset_sequence=0;
    if(value==sequence[reset_sequence]) {
        if(!reset_sequence)reset_started=now;
        if(++reset_sequence==4){reset_sequence=0;device_request_restart(true);}
    } else {reset_sequence=value==1?1:0;if(value==1)reset_started=now;}
}
esp_err_t usb_keyboard_start(void) {
    uint8_t mac[6]; esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const tinyusb_config_t config = {
        .device_descriptor = NULL, .string_descriptor = strings,
        .string_descriptor_count = sizeof(strings) / sizeof(strings[0]), .external_phy = false,
        .configuration_descriptor = configuration
    };
    err = tinyusb_driver_install(&config);
    if (err != ESP_OK) return err;
    tinyusb_config_cdcacm_t cdc = {.usb_dev = TINYUSB_USBDEV_0, .cdc_port = TINYUSB_CDC_ACM_0, .rx_unread_buf_sz = 2048, .callback_line_state_changed=cdc_line_state, .callback_line_coding_changed=cdc_line_coding};
    err = tusb_cdc_acm_init(&cdc);
    if (err == ESP_OK) err = esp_tusb_init_console(TINYUSB_CDC_ACM_0);
    if (err == ESP_OK) err = usb_control_start();
    return err;
}
