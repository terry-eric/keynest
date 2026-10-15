#include "app.h"
#include "macro_store.h"
#include "macro_engine.h"
#include "tinyusb.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint64_t clock_ms;
static bool mounted=true, suspended, ready=true, busy;
static unsigned completed,failed,dropped,reports;
static char programs[4][1201];
static macro_frame_t output;
static unsigned chord_count,media_count,a_count,b_count;
int64_t esp_timer_get_time(void){return clock_ms*1000;}
bool tud_mounted(void){return mounted;}
bool tud_suspended(void){return suspended;}
bool tud_hid_ready(void){return ready;}
bool tud_hid_report(uint8_t id,const void *data,uint16_t n){
 assert(ready);++reports;
 if(id==1){assert(n==29);memcpy(&output,data,n);if(output.modifiers==1 && (output.keys[6/8]&(1<<(6%8))))++chord_count;if(output.keys[0]&(1<<4))++a_count;if(output.keys[0]&(1<<5))++b_count;}
 else {assert(id==2&&n==1);output.media=*(const uint8_t*)data;if(output.media)++media_count;}
 return true;
}
uint8_t macro_store_mask(void){uint8_t result=0;for(unsigned i=0;i<4;i++)if(*programs[i])result|=1U<<i;return result;}
uint32_t macro_store_gestures(uint16_t hold[4],uint16_t twice[4]){
 for(unsigned i=0;i<4;i++){hold[i]=twice[i]=0;cJSON *p=cJSON_Parse(programs[i]),*g=cJSON_GetObjectItemCaseSensitive(p,"gestures");
 if(g){hold[i]=(uint16_t)cJSON_GetObjectItemCaseSensitive(g,"hold_ms")->valueint;twice[i]=(uint16_t)cJSON_GetObjectItemCaseSensitive(g,"double_ms")->valueint;}cJSON_Delete(p);}return 0;
}
cJSON *macro_store_get(unsigned key){return cJSON_Parse(programs[key]);}
void macro_store_execution(bool b,unsigned key,const char *result){(void)key;busy=b;if(!strcmp(result,"completed"))completed++;else if(!strcmp(result,"busy"))dropped++;else if(!b)failed++;}
void logic_hid_report(uint8_t down,const keypad_config_t *c,keypad_report_t *r){memset(r,0,sizeof(*r));unsigned n=0;for(unsigned i=0;i<4;i++)if(down&(1U<<i)){r->modifiers|=c->modifiers[i];r->keys[n++]=c->keycodes[i];}}
esp_err_t device_request_restart(bool boot){(void)boot;return 0;}
esp_err_t usb_control_start(void){return 0;}
esp_err_t esp_read_mac(uint8_t *out,int type){(void)type;memset(out,0,6);return 0;}
esp_err_t tinyusb_driver_install(const tinyusb_config_t *c){(void)c;return 0;}
esp_err_t tusb_cdc_acm_init(const tinyusb_config_cdcacm_t *c){(void)c;return 0;}
esp_err_t esp_tusb_init_console(int c){(void)c;return 0;}
static keypad_config_t config={.keycodes={104,105,106,107}};
static void ticks(unsigned n,uint8_t down){while(n--){clock_ms+=5;usb_keyboard_tick(down,&config);}}
static bool empty(void){macro_frame_t z={0};return !memcmp(&output,&z,sizeof(z));}
static void save(unsigned key,const char *steps){snprintf(programs[key],sizeof(programs[key]),"{\"name\":\"test\",\"steps\":%s}",steps);}
int main(void){
 ticks(10,0);assert(reports==2&&empty());
 ticks(2,2);assert(output.keys[105/8]&(1<<(105%8)));ticks(2,0);assert(empty());
 save(0,"[{\"type\":\"keys\",\"keys\":[\"CTRL\",\"C\"]}]");
 ticks(100,1);assert(completed==1&&chord_count==1&&empty());ticks(10,0);
 // Changing stored program after the edge must not change the running snapshot.
 save(0,"[{\"type\":\"wait\",\"ms\":100},{\"type\":\"keys\",\"keys\":[\"CTRL\",\"C\"]}]");
 ticks(1,1);save(0,"[{\"type\":\"keys\",\"keys\":[\"A\"]}]");ticks(100,0);assert(completed==2&&chord_count==2&&empty());
 save(0,"[{\"type\":\"keys\",\"keys\":[\"A\",\"B\",\"C\",\"D\",\"E\",\"F\",\"G\",\"H\",\"I\",\"J\",\"K\",\"L\",\"M\",\"N\",\"O\",\"MUTE\"]}]");
 ticks(3,1);for(unsigned c=4;c<19;c++)assert(output.keys[c/8]&(1<<(c%8)));assert(output.media==1);
 ticks(100,0);assert(completed==3&&media_count==1&&empty());
 save(0,"[{\"type\":\"keys\",\"keys\":[\"CTRL\",\"C\"]}]");ready=false;ticks(200,1);assert(busy);unsigned before=chord_count;ready=true;ticks(100,0);assert(completed==4&&chord_count==before+1&&empty());
 // Multiple macro presses are dropped while busy, with no stale queued action.
 save(0,"[{\"type\":\"wait\",\"ms\":1000},{\"type\":\"keys\",\"keys\":[\"CTRL\",\"C\"]}]");save(1,"[{\"type\":\"keys\",\"keys\":[\"A\"]}]");
 ticks(2,1);ticks(2,3);assert(busy&&dropped==1);ticks(300,0);assert(completed==5&&empty());
 // A disconnect cancels a wait. A held key on reconnect does not replay it.
 ticks(2,1);mounted=false;ticks(2,1);assert(!busy&&failed==1);mounted=true;ticks(300,1);assert(completed==5&&empty());ticks(2,0);
 // Ordinary physical input cancels the running macro instead of contaminating text.
 ticks(2,1);ticks(3,4);assert(!busy&&failed==2);assert(output.keys[106/8]&(1<<(106%8)));ticks(300,0);assert(completed==5&&empty());
 // If the host stops accepting HID, abort and release instead of replaying later.
 save(0,"[{\"type\":\"keys\",\"keys\":[\"CTRL\",\"C\"]}]");ready=false;ticks(500,1);assert(!busy&&failed==3);ready=true;ticks(100,0);assert(empty()&&completed==5);
 // Cancellation's all-up frame must survive backpressure, even if manual C overlaps macro C.
 config.keycodes[2]=6;ticks(1,1);assert(output.modifiers==1);ready=false;ticks(1,4);assert(!busy);ready=true;
 ticks(1,4);assert(empty());ticks(3,4);assert(output.modifiers==0&&(output.keys[0]&(1<<6)));ticks(5,0);assert(empty());
 // Gesture actions are mutually exclusive; taps wait only when double-click is enabled.
 memset(programs,0,sizeof(programs));mounted=false;ticks(2,0);mounted=true;ticks(10,0);
 strcpy(programs[0],"{\"name\":\"tap\",\"steps\":[{\"type\":\"keys\",\"keys\":[\"CTRL\",\"C\"]}],\"gestures\":{\"hold_ms\":600,\"double_ms\":300,\"hold\":{\"name\":\"hold\",\"steps\":[{\"type\":\"keys\",\"keys\":[\"B\"]}]},\"double\":{\"name\":\"double\",\"steps\":[{\"type\":\"keys\",\"keys\":[\"A\"]}]}}}");
 unsigned done=completed,copied=chord_count,ac=a_count,bc=b_count;
 ticks(20,1);ticks(50,0);assert(completed==done&&chord_count==copied);
 ticks(60,0);assert(completed==done+1&&chord_count==copied+1);done=completed;
 ticks(10,1);ticks(10,0);ticks(10,1);ticks(100,0);
 assert(completed==done+1&&a_count==ac+1&&chord_count==copied+1);done=completed;
 ticks(100,1);assert(completed==done);ticks(200,1);assert(completed==done+1&&b_count==bc+1);ticks(100,0);assert(completed==done+1);done=completed;
 // A second press held past the threshold is a hold, never a double plus hold.
 ticks(10,1);ticks(10,0);ticks(200,1);ticks(100,0);
 assert(completed==done+1&&b_count==bc+2&&a_count==ac+1);done=completed;
 // Pending taps are discarded on disconnect and manual-key interruption.
 ticks(10,1);ticks(10,0);mounted=false;ticks(10,0);mounted=true;ticks(100,0);assert(completed==done);
 ticks(10,1);ticks(10,0);ticks(10,4);ticks(100,0);assert(completed==done);
 // Modifier-only raw keys still combine with a separately held A and release independently.
 memset(programs,0,sizeof(programs));config.keycodes[0]=0;config.modifiers[0]=1;config.keycodes[1]=4;
 ticks(5,1);assert(output.modifiers==1);ticks(5,3);assert(output.modifiers==1&&(output.keys[0]&(1<<4)));
 ticks(5,2);assert(output.modifiers==0&&(output.keys[0]&(1<<4)));ticks(5,0);assert(empty());
 puts("PASS: actual USB scheduler initialization, no repeat on hold, snapshot isolation, NKRO+media release, HID backpressure, busy drops, disconnect/no replay, manual interruption, USB timeout and persistent all-up before overlapping manual input");return 0;
}
