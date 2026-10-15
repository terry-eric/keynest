#include "macro_engine.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static cJSON *program;
static macro_engine_t engine;
static macro_frame_t frame;
static unsigned hold;
static const char *start(const char *raw){cJSON_Delete(program);program=cJSON_Parse(raw);assert(program);return macro_engine_start(&engine,program);}
static bool next(bool caps){return macro_engine_next(&engine,&frame,&hold,caps);}
static bool key(unsigned code){return !!(frame.keys[code/8]&(1U<<(code%8)));}
static void released(void){assert(next(false));macro_frame_t zero={0};assert(!memcmp(&frame,&zero,sizeof(frame)));}
int main(void){
 assert(!start("{\"name\":\"\",\"steps\":[{\"type\":\"keys\",\"keys\":[\"CTRL\",\"A\",\"B\",\"C\",\"D\",\"E\",\"F\",\"G\",\"H\",\"I\",\"J\",\"K\",\"L\",\"M\",\"N\",\"O\"]},{\"type\":\"keys\",\"keys\":[\"MUTE\",\"PLAYPAUSE\",\"VOLUMEUP\",\"VOLUMEDOWN\",\"NEXT\",\"PREVIOUS\"]}]}"));
 assert(next(false));assert(frame.modifiers==1);for(unsigned c=4;c<19;c++)assert(key(c));released();
 assert(next(false));assert(frame.media==63);released();assert(!next(false));
 assert(!start("{\"name\":\"\",\"steps\":[{\"type\":\"text\",\"text\":\"aA!0\\n\\t~?\"},{\"type\":\"wait\",\"ms\":4321}]}"));
 const unsigned codes[]={4,4,30,39,40,43,53,56},mods[]={0,2,2,0,0,0,2,2};
 for(unsigned i=0;i<8;i++){assert(next(false));assert(key(codes[i]));assert(frame.modifiers==mods[i]);released();}
 assert(next(false)&&hold==4321);assert(!next(false));
 assert(!start("{\"name\":\"\",\"steps\":[{\"type\":\"text\",\"text\":\"aA\"}]}"));
 assert(next(true)&&key(4)&&frame.modifiers==2);released();assert(next(true)&&key(4)&&frame.modifiers==0);released();assert(!next(false));
 // Preflight must refuse the whole action before even its initial Ctrl+C chord.
 assert(!strcmp(start("{\"name\":\"\",\"steps\":[{\"type\":\"keys\",\"keys\":[\"CTRL\",\"C\"]},{\"type\":\"text\",\"text\":\"中文\"}]}"),"unicode_mode_required"));
 assert(!start("{\"name\":\"\",\"text_mode\":\"linux\",\"steps\":[{\"type\":\"text\",\"text\":\"中😀\"}]}"));
 const unsigned linux[]={24,33,8,31,7,40,24,30,9,35,39,39,40};
 for(unsigned i=0;i<13;i++){assert(next(false));assert(key(linux[i]));assert(frame.modifiers==((i==0||i==6)?3:0));released();}assert(!next(false));
 assert(!start("{\"name\":\"\",\"text_mode\":\"macos\",\"steps\":[{\"type\":\"text\",\"text\":\"中\"}]}"));
 const unsigned mac[]={33,8,31,7};
 for(unsigned i=0;i<4;i++){assert(next(false));assert(key(mac[i])&&frame.modifiers==4);assert(next(false));assert(frame.modifiers==(i<3?4:0));for(unsigned j=0;j<28;j++)assert(!frame.keys[j]);}assert(!next(false));
 assert(!strcmp(start("{\"name\":\"\",\"text_mode\":\"macos\",\"steps\":[{\"type\":\"text\",\"text\":\"中😀\"}]}"),"macos_bmp_only"));
 assert(!start("{\"name\":\"\",\"steps\":[{\"type\":\"hid\",\"code\":4,\"modifiers\":128}]}"));assert(next(false)&&key(4)&&frame.modifiers==128);released();assert(!next(false));
 cJSON_Delete(program);puts("PASS: real macro engine 16-key NKRO, all media bits, ASCII/punctuation, Caps Lock, waits, Linux Unicode, macOS modifier holds, full-action preflight");return 0;
}
