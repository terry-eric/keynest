#include "macro_engine.h"
#include <string.h>
#include <stdio.h>

static const cJSON *get(const cJSON *p, const char *key) { return cJSON_GetObjectItemCaseSensitive(p,key); }
void macro_frame_key(macro_frame_t *f, uint8_t code) {
    if(code>=0xe0 && code<=0xe7) f->modifiers|=1U<<(code-0xe0);
    else if(code>=4 && code<0xe0) f->keys[code/8]|=1U<<(code%8);
}
static uint32_t character(const unsigned char **text) {
    const unsigned char *p=*text; uint32_t c=*p++;
    unsigned n=0;
    if(c>=0xf0){c&=7;n=3;}else if(c>=0xe0){c&=15;n=2;}else if(c>=0xc0){c&=31;n=1;}
    while(n--)c=(c<<6)|(*p++&63);
    *text=p;return c;
}
static uint8_t ascii(unsigned c, uint8_t *mods, bool caps) {
    *mods=0;
    if(c>='a' && c<='z'){*mods=caps?2:0;return 4+c-'a';}
    if(c>='A' && c<='Z'){*mods=caps?0:2;return 4+c-'A';}
    if(c>='1' && c<='9')return 30+c-'1';
    if(c=='0')return 39;
    if(c=='\n')return 40;
    if(c=='\t')return 43;
    if(c==' ')return 44;
    const char *a="-=[]\\;'`,./",*b="_+{}|:\"~<>?";
    static const uint8_t codes[]={45,46,47,48,49,51,52,53,54,55,56};
    const char *p=strchr(a,(int)c);if(p)return codes[p-a];
    p=strchr(b,(int)c);if(p){*mods=2;return codes[p-b];}
    const char *digits="!@#$%^&*()";
    p=strchr(digits,(int)c);if(p){*mods=2;return p[0]==')'?39:30+(p-digits);}
    return 0;
}
static void named(macro_frame_t *f,const char *s) {
    uint8_t mod=0,code=0;
    if(strlen(s)==1){code=ascii((unsigned char)*s,&mod,false);mod=0;}
    else if(s[0]=='F' && s[1]>='1' && s[1]<='9') { unsigned n=0;sscanf(s+1,"%u",&n);if(n<=24)code=n<=12?57+n:91+n; }
    const char *const modifiers[]={"CTRL","SHIFT","ALT","META","RCTRL","RSHIFT","RALT","RMETA"};
    for(unsigned i=0;i<8;i++)if(!strcmp(s,modifiers[i]))mod|=1U<<i;
    const char *const names[]={"ENTER","ESC","BACKSPACE","TAB","SPACE","CAPSLOCK","PRINTSCREEN","SCROLLLOCK","PAUSE","INSERT","HOME","PAGEUP","DELETE","END","PAGEDOWN","RIGHT","LEFT","DOWN","UP","NUMLOCK"};
    static const uint8_t codes[]={40,41,42,43,44,57,70,71,72,73,74,75,76,77,78,79,80,81,82,83};
    for(unsigned i=0;i<sizeof(codes);i++)if(!strcmp(s,names[i]))code=codes[i];
    const char *const media[]={"MUTE","VOLUMEUP","VOLUMEDOWN","PLAYPAUSE","NEXT","PREVIOUS"};
    for(unsigned i=0;i<6;i++)if(!strcmp(s,media[i]))f->media|=1U<<i;
    f->modifiers|=mod;macro_frame_key(f,code);
}
const char *macro_engine_start(macro_engine_t *e,const cJSON *p) {
    memset(e,0,sizeof(*e));const cJSON *mode=get(p,"text_mode");
    if(cJSON_IsString(mode))e->mode=!strcmp(mode->valuestring,"linux")?1:!strcmp(mode->valuestring,"macos")?2:0;
    const cJSON *steps=get(p,"steps");
    for(const cJSON *s=steps?steps->child:NULL;s;s=s->next){
        const cJSON *t=get(s,"text");if(!cJSON_IsString(t))continue;
        const unsigned char *q=(const unsigned char*)t->valuestring;
        while(*q){uint32_t c=character(&q);if(c>127 && !e->mode)return "unicode_mode_required";
            if(c>0xffff && e->mode==2)return "macos_bmp_only";}
    }
    e->step=steps?steps->child:NULL;return NULL;
}
static void stroke(macro_engine_t *e,uint8_t code,uint8_t mods){e->code[e->count]=code;e->mods[e->count++]=mods;}
static void text_strokes(macro_engine_t *e,uint32_t c,bool caps){
    e->cursor=e->count=0;
    uint8_t mods=0;
    if(c<128){uint8_t code=ascii(c,&mods,caps);stroke(e,code,mods);return;}
    char hex[9];snprintf(hex,sizeof(hex),e->mode==2?"%04lx":"%lx",(unsigned long)c);
    if(e->mode==1)stroke(e,24,3); // Ctrl+Shift+U, then release both modifiers.
    for(const char *p=hex;*p;p++){uint8_t code=ascii((unsigned char)*p,&mods,caps);stroke(e,code,e->mode==2?(mods|4):mods);}
    if(e->mode==1)stroke(e,40,0);
}
bool macro_engine_next(macro_engine_t *e,macro_frame_t *f,unsigned *hold,bool caps){
    memset(f,0,sizeof(*f));*hold=30;
    // Always release after each stroke/chord, including the final step.
    if(e->release){e->release=false;if(e->mode==2 && e->cursor<e->count)f->modifiers=4;return true;}
    for(;;){
        if(e->cursor<e->count){f->modifiers=e->mods[e->cursor];macro_frame_key(f,e->code[e->cursor++]);e->release=true;return true;}
        if(e->text && *e->text){text_strokes(e,character(&e->text),caps);continue;}
        e->text=NULL;e->count=e->cursor=0;
        if(!e->step)return false;
        const cJSON *s=e->step;e->step=s->next;const cJSON *t=get(s,"type");
        if(!strcmp(t->valuestring,"wait")){*hold=(unsigned)get(s,"ms")->valuedouble;return true;}
        if(!strcmp(t->valuestring,"text")){e->text=(const unsigned char*)get(s,"text")->valuestring;continue;}
        const cJSON *keys=get(s,"keys");for(const cJSON *k=keys->child;k;k=k->next)named(f,k->valuestring);
        e->release=true;return true;
    }
}
