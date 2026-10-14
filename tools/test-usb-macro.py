"""Test the actual USB tick scheduler with fake TinyUSB; never sends host input."""
from pathlib import Path
import subprocess
import tempfile
import re
ROOT=Path(__file__).resolve().parents[1]
CJSON=ROOT/'.tools/platformio/packages/framework-espidf/components/json/cJSON'
TCC=ROOT/'.tools/tcc/tcc/tcc.exe'
source=(ROOT/'firmware/keyboard/main/usb_keyboard.c').read_text(encoding='utf-8')
descriptor=source.split('static const uint8_t report_descriptor[] = {',1)[1].split('};',1)[0]
descriptor=re.sub(r'//[^\n]*','',descriptor)
raw=bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]+)',descriptor))
size=count=report=0;inputs={};outputs={};offset=0
while offset<len(raw):
    tag=raw[offset];length=tag&3;length=4 if length==3 else length
    value=int.from_bytes(raw[offset+1:offset+1+length],'little');offset+=1+length
    if tag==0x75:size=value
    elif tag==0x95:count=value
    elif tag==0x85:report=value
    elif tag&0xfc==0x80:inputs[report]=inputs.get(report,0)+size*count
    elif tag&0xfc==0x90:outputs[report]=outputs.get(report,0)+size*count
assert inputs=={1:232,2:8} and outputs=={1:8},(inputs,outputs)
assert 'HID_ITF_PROTOCOL_NONE' in source
with tempfile.TemporaryDirectory(prefix='hid-macro-',dir=ROOT/'.tools') as folder:
    temp=Path(folder)
    (temp/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n')
    (temp/'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
    common='''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define TUD_CONFIG_DESCRIPTOR(...) 0
#define TUD_CDC_DESCRIPTOR(...) 0
#define TUD_HID_DESCRIPTOR(...) 0
#define HID_REPORT_TYPE_INPUT 1
#define HID_REPORT_TYPE_OUTPUT 2
#define ESP_MAC_WIFI_STA 0
#define TINYUSB_USBDEV_0 0
#define TINYUSB_CDC_ACM_0 0
#define CFG_TUD_HID_EP_BUFSIZE 64
// TinyCC predates C11; retain equivalent compile-time size checks.
#define _Static_assert(condition,message) extern char pcb2_compile_assert[(condition)?1:-1]
typedef int hid_report_type_t;
typedef struct {const void *device_descriptor;const char **string_descriptor;unsigned string_descriptor_count;bool external_phy;const void *configuration_descriptor;} tinyusb_config_t;
typedef struct {unsigned bit_rate;} coding_t;
typedef struct {struct {coding_t *p_line_coding;} line_coding_changed_data;struct {bool dtr,rts;} line_state_changed_data;} cdcacm_event_t;
typedef struct {unsigned usb_dev,cdc_port,rx_unread_buf_sz;void (*callback_line_state_changed)(int,cdcacm_event_t*);void (*callback_line_coding_changed)(int,cdcacm_event_t*);} tinyusb_config_cdcacm_t;
bool tud_mounted(void);bool tud_suspended(void);bool tud_hid_ready(void);
bool tud_hid_report(uint8_t,const void*,uint16_t);
esp_err_t tinyusb_driver_install(const tinyusb_config_t*);
esp_err_t tusb_cdc_acm_init(const tinyusb_config_cdcacm_t*);
esp_err_t esp_tusb_init_console(int);
esp_err_t esp_read_mac(uint8_t*,int);
'''
    (temp/'tinyusb.h').write_text(common)
    for name in ['tusb_cdc_acm.h','tusb_console.h','esp_mac.h']:(temp/name).write_text('#include "tinyusb.h"\n')
    binary=temp/'test.exe'
    sources=['tools/test-usb-macro.c','firmware/keyboard/main/usb_keyboard.c','firmware/keyboard/main/macro_engine.c']
    subprocess.run([str(TCC),'-DCJSON_HIDE_SYMBOLS','-D__cdecl=','-D__stdcall=','-I'+str(temp),'-I'+str(CJSON),'-I'+str(ROOT/'firmware/keyboard/main'),*[str(ROOT/s) for s in sources],str(CJSON/'cJSON.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
