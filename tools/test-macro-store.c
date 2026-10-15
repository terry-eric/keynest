#include "macro_store.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int64_t now=100;
static int fail_write;
static char saved[4][MACRO_PROGRAM_BYTES+1];
int64_t esp_timer_get_time(void){return now;}
void *xSemaphoreCreateMutex(void){return (void*)1;}
int xSemaphoreTake(void *p,unsigned n){(void)p;(void)n;return 1;}
int xSemaphoreGive(void *p){(void)p;return 1;}
int nvs_open(const char *name,int mode,nvs_handle_t *handle){(void)name;(void)mode;*handle=1;return 0;}
void nvs_close(nvs_handle_t handle){(void)handle;}
int nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *n){(void)h;unsigned i=key[3]-'0';if(!*saved[i])return 1;assert(strlen(saved[i])+1<=*n);strcpy(out,saved[i]);*n=strlen(out)+1;return 0;}
int nvs_set_str(nvs_handle_t h,const char *key,const char *text){(void)h;if(fail_write)return 5;strcpy(saved[key[3]-'0'],text);return 0;}
int nvs_commit(nvs_handle_t h){(void)h;return 0;}
static int command(const char *path,const char *raw){cJSON *j=cJSON_Parse(raw);int result=macro_store_command(path,j);cJSON_Delete(j);return result;}
static int valid(const char *raw){cJSON *j=cJSON_Parse(raw);int result=macro_program_valid(j);cJSON_Delete(j);return result;}
static unsigned pending_id(void){cJSON *j=macro_store_event();cJSON *v=cJSON_GetObjectItemCaseSensitive(j,"id");unsigned id=v?(unsigned)v->valuedouble:0;cJSON_Delete(j);return id;}
int main(void){
 assert(!macro_json_safe("\"x\\u0000y\"",10));
 assert(macro_json_safe("\"x\\\\u0000y\"",11));
 assert(macro_store_init()==0);
 assert(valid("{\"name\":\"中文\",\"steps\":[{\"type\":\"text\",\"text\":\"你好😀\\n\"}]}"));
 const char *invalid[]={"{\"name\":\"\",\"steps\":[{\"type\":\"wait\",\"ms\":true}]}","{\"name\":\"\",\"steps\":[{\"type\":\"keys\",\"keys\":[\"A\",\"A\"]}]}","{\"name\":\"\",\"name\":\"x\",\"steps\":[]}","{\"name\":\"\",\"steps\":[{\"type\":\"text\",\"text\":\"\"}]}","{\"name\":\"\",\"steps\":[{\"type\":\"exec\",\"command\":\"x\"}]}"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)assert(!valid(invalid[i]));
 assert(command("/api/macro","{\"key\":0,\"program\":{\"name\":\"hello\",\"steps\":[{\"type\":\"keys\",\"keys\":[\"CTRL\",\"A\",\"B\",\"C\",\"D\",\"E\",\"F\",\"G\"]},{\"type\":\"text\",\"text\":\"你好\"}]}}") == 0);
 assert(macro_store_mask()==1);macro_store_press(0);assert(!pending_id());
 assert(command("/api/macro/host","{\"token\":\"0123456789abcdef\",\"busy\":false}")!=0);
 macro_store_press(0);assert(!pending_id());
 cJSON *snapshot=macro_store_get(0);assert(snapshot);assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(snapshot,"steps"))==2);
 assert(valid("{\"name\":\"\",\"text_mode\":\"linux\",\"steps\":[]}"));
 assert(!valid("{\"name\":\"\",\"text_mode\":\"windows\",\"steps\":[]}"));
 macro_store_execution(true,0,"running");macro_store_execution(true,1,"busy");macro_store_execution(false,0,"completed");
 cJSON *status=macro_store_status();assert(!strcmp(cJSON_GetObjectItemCaseSensitive(status,"execution")->valuestring,"device"));
 assert(cJSON_GetObjectItemCaseSensitive(status,"completed")->valueint==1);assert(cJSON_GetObjectItemCaseSensitive(status,"dropped")->valueint==1);cJSON_Delete(status);
 fail_write=1;assert(command("/api/macro","{\"key\":0,\"program\":{\"name\":\"\",\"steps\":[]}}")!=0);assert(macro_store_mask()==1);
 fail_write=0;assert(command("/api/macro","{\"key\":0,\"program\":{\"name\":\"\",\"steps\":[]}}") ==0);assert(macro_store_mask()==0);
 assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(snapshot,"steps"))==2);cJSON_Delete(snapshot);
 const char *gesture="{\"key\":2,\"program\":{\"name\":\"\",\"steps\":[{\"type\":\"hid\",\"code\":4,\"modifiers\":128}],\"gestures\":{\"hold_ms\":600,\"double_ms\":300,\"hold\":{\"name\":\"hold\",\"steps\":[{\"type\":\"keys\",\"keys\":[\"CTRL\",\"A\"]}]},\"double\":null}}}";
 assert(command("/api/macro",gesture)==0);assert(macro_store_mask()==4);
 uint16_t h[4],d[4];unsigned rev=macro_store_gestures(h,d);assert(h[2]==600&&d[2]==0&&rev>0);
 snapshot=macro_store_get(2);assert(macro_program_valid(snapshot));cJSON *g=cJSON_GetObjectItemCaseSensitive(snapshot,"gestures");
 cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(g,"hold_ms"),299);assert(!macro_program_valid(snapshot));
 cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(g,"hold_ms"),600);
 cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(g,"double_ms"),601);assert(!macro_program_valid(snapshot));
 cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(g,"double_ms"),300);
 cJSON *nested=cJSON_Duplicate(g,true);cJSON_AddItemToObject(cJSON_GetObjectItemCaseSensitive(g,"hold"),"gestures",nested);assert(!macro_program_valid(snapshot));cJSON_Delete(snapshot);
 // Reload from the same simulated NVS: gestures and masks survive reboot.
 assert(macro_store_init()==0);snapshot=macro_store_get(2);assert(macro_program_valid(snapshot));cJSON_Delete(snapshot);
 assert(macro_store_gestures(h,d)>rev&&h[2]==600&&d[2]==0);
 puts("PASS: C Unicode validation, NVS failures, device execution state, independent running snapshot and disabled legacy host input");return 0;
}
