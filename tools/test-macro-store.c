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
 assert(command("/api/macro/host","{\"token\":\"0123456789abcdef\",\"busy\":false}")==0);
 assert(command("/api/macro/host","{\"token\":\"1123456789abcdef\",\"busy\":false}")!=0);
 macro_store_press(0);unsigned id=pending_id();assert(id);macro_store_press(0);assert(pending_id()==id);
 assert(command("/api/macro/ack","{\"token\":\"1123456789abcdef\",\"id\":1}")!=0);assert(pending_id()==id);
 char body[90];snprintf(body,sizeof(body),"{\"token\":\"0123456789abcdef\",\"id\":%u}",id);assert(command("/api/macro/ack",body)==0);assert(!pending_id());assert(command("/api/macro/ack",body)!=0);
 assert(command("/api/macro/host","{\"token\":\"0123456789abcdef\",\"busy\":true}")==0);macro_store_press(0);assert(!pending_id());
 assert(command("/api/macro/host","{\"token\":\"0123456789abcdef\",\"busy\":false}")==0);macro_store_press(0);assert(pending_id());now+=2000001;assert(!pending_id());
 assert(command("/api/macro/host","{\"token\":\"1123456789abcdef\",\"busy\":false}")==0);assert(!pending_id());
 fail_write=1;assert(command("/api/macro","{\"key\":0,\"program\":{\"name\":\"\",\"steps\":[]}}")!=0);assert(macro_store_mask()==1);
 fail_write=0;assert(command("/api/macro","{\"key\":0,\"program\":{\"name\":\"\",\"steps\":[]}}") ==0);assert(macro_store_mask()==0);
 puts("PASS: C Unicode validation, NVS failures, host lease, busy/drop, acknowledgment and expiry without stale replay");return 0;
}
