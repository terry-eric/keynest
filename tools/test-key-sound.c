// Exercise the maintained implementation, including storage and playback routing.
#include "../firmware/keyboard/main/key_sound.c"
#define CHECK(c) do { if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);exit(1);} } while(0)
static uint8_t flash[PER_KEY_BYTES],before[PER_KEY_BYTES];
static esp_partition_t fake_partition={PER_KEY_BYTES};
static int fail_at=-1,operations;
typedef struct {char name[16],value[32];uint32_t number;bool used;} entry_t;
static entry_t saved[24],staged[24];
const esp_partition_t *esp_partition_find_first(int a,int b,const char *name){(void)a;(void)b;return !strcmp(name,"key_sound")?&fake_partition:NULL;}
esp_err_t esp_partition_read(const esp_partition_t*p,size_t at,void*out,size_t n){if(at+n>p->size)return ESP_ERR_INVALID_ARG;memcpy(out,flash+at,n);return ESP_OK;}
esp_err_t esp_partition_erase_range(const esp_partition_t*p,size_t at,size_t n){if(at%4096||n%4096||at+n>p->size)return ESP_ERR_INVALID_ARG;bool fail=operations++==fail_at;memset(flash+at,255,fail?n/2:n);return fail?ESP_FAIL:ESP_OK;}
esp_err_t esp_partition_write(const esp_partition_t*p,size_t at,const void*in,size_t n){if(at+n>p->size)return ESP_ERR_INVALID_ARG;bool fail=operations++==fail_at;const uint8_t*bytes=in;for(size_t i=0;i<(fail?n/2:n);++i)flash[at+i]&=bytes[i];return fail?ESP_FAIL:ESP_OK;}
const char *esp_err_to_name(esp_err_t e){(void)e;return "test error";}
int64_t esp_timer_get_time(void){return 1000;}
void recorder_snapshot(recorder_status_t*out){memset(out,0,sizeof(*out));out->state=REC_IDLE;}
esp_err_t recorder_retry_mount(void){return ESP_OK;}
static entry_t *entry(entry_t*list,const char*name,bool create){for(unsigned i=0;i<24;++i)if(list[i].used&&!strcmp(list[i].name,name))return &list[i];if(create)for(unsigned i=0;i<24;++i)if(!list[i].used){list[i].used=true;snprintf(list[i].name,16,"%s",name);return &list[i];}return NULL;}
esp_err_t nvs_open(const char*n,int mode,nvs_handle_t*h){(void)n;*h=mode;if(mode==NVS_READWRITE)memcpy(staged,saved,sizeof(saved));return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;memcpy(saved,staged,sizeof(saved));return ESP_OK;}
esp_err_t nvs_get_u32(nvs_handle_t h,const char*n,uint32_t*out){(void)h;entry_t*e=entry(saved,n,false);if(!e)return ESP_ERR_NOT_FOUND;*out=e->number;return ESP_OK;}
esp_err_t nvs_get_u8(nvs_handle_t h,const char*n,uint8_t*out){uint32_t x=0;esp_err_t e=nvs_get_u32(h,n,&x);if(e==ESP_OK)*out=x;return e;}
esp_err_t nvs_set_u32(nvs_handle_t h,const char*n,uint32_t x){(void)h;entry_t*e=entry(staged,n,true);if(!e)return ESP_ERR_NO_MEM;e->number=x;return ESP_OK;}
esp_err_t nvs_set_u8(nvs_handle_t h,const char*n,uint8_t x){return nvs_set_u32(h,n,x);}
esp_err_t nvs_get_str(nvs_handle_t h,const char*n,char*out,size_t*size){(void)h;entry_t*e=entry(saved,n,false);if(!e)return ESP_ERR_NOT_FOUND;size_t bytes=strlen(e->value)+1;if(*size<bytes)return ESP_FAIL;memcpy(out,e->value,bytes);*size=bytes;return ESP_OK;}
esp_err_t nvs_set_str(nvs_handle_t h,const char*n,const char*value){(void)h;entry_t*e=entry(staged,n,true);if(!e)return ESP_ERR_NO_MEM;snprintf(e->value,32,"%s",value);return ESP_OK;}
static void reboot(void){for(unsigned i=0;i<5;++i)sample_free(banks[i].pcm);sample_free(pending);memset(banks,0,sizeof(banks));memset(&status,0,sizeof(status));pending=NULL;pending_owner[0]=committed_owner[0]=0;CHECK(key_sound_init()==ESP_OK);}
static void upload(int key,int16_t*pcm,uint32_t frames,bool commit){const char*owner="0123456789abcdef";uint8_t*bytes=(uint8_t*)pcm;CHECK(key_sound_begin(owner,REC_FLASH,frames,logic_crc32(0,pcm,frames*2),key)==ESP_OK);CHECK(key_sound_begin(owner,REC_FLASH,frames,logic_crc32(0,pcm,frames*2),key)==ESP_OK);for(uint32_t offset=0;offset<frames*2;offset+=1024){size_t n=frames*2-offset;if(n>1024)n=1024;uint32_t crc=logic_crc32(0,bytes+offset,n);CHECK(key_sound_chunk(owner,offset,bytes+offset,n,crc)==ESP_OK);CHECK(key_sound_chunk(owner,offset,bytes+offset,n,crc)==ESP_OK);}if(commit){CHECK(key_sound_commit(owner)==ESP_OK);CHECK(key_sound_commit(owner)==ESP_OK);}}
int main(void){
    int16_t pcm[32000];for(unsigned i=0;i<32000;++i)pcm[i]=(i%32<16?15000:-15000);
    memset(flash,255,sizeof(flash));fake_partition.size=0x40000;reboot();
    CHECK(key_sound_begin("0123456789abcdef",REC_FLASH,32000,0,0)==ESP_ERR_NOT_SUPPORTED);
    upload(-1,pcm,32000,true);memcpy(before,flash,sizeof(flash));
    fake_partition.size=PER_KEY_BYTES;reboot();key_sound_status_t snapshot;key_sound_snapshot(&snapshot);
    CHECK(snapshot.ready&&snapshot.per_key&&!snapshot.keys[0].custom&&snapshot.keys[0].frames==32000);
    uint32_t crcs[4];for(unsigned key=0;key<4;++key){for(unsigned i=0;i<32000;++i)pcm[i]=(i%32<16?20000:-20000)+(int)key*100;crcs[key]=logic_crc32(0,pcm,sizeof(pcm));upload(key,pcm,32000,true);}
    CHECK(!memcmp(flash,before,0x40000));reboot();key_sound_snapshot(&snapshot);
    for(unsigned key=0;key<4;++key)CHECK(snapshot.keys[key].custom&&snapshot.keys[key].crc32==crcs[key]);
    for(unsigned key=0;key<4;++key){
        for(unsigned i=0;i<4;++i)key_sound_stop(i);
        key_sound_trigger(key);int32_t isolated[145]={0};key_sound_mix(isolated,145,100);
        CHECK(isolated[144]==-20000+(int)key*100);
    }
    for(unsigned key=0;key<4;++key){key_sound_trigger(key);CHECK(voice_bank(key)==&banks[key]);}
    int32_t mix[1024]={0};key_sound_mix(mix,1024,100);keypad_limiter_t limiter={65536};bool overload=false;
    for(unsigned i=0;i<1024;++i){overload|=mix[i]>32767||mix[i]<-32768;int out=logic_limit_sample(&limiter,mix[i]);CHECK(out>=-28000&&out<=28000);}CHECK(overload);
    CHECK(key_sound_begin("0123456789abcdef",REC_FLASH,100,0,5)==ESP_ERR_INVALID_ARG);
    CHECK(key_sound_begin("0123456789abcdef",REC_FLASH,100,0,0)==ESP_OK);
    CHECK(key_sound_begin("0123456789abcdef",REC_FLASH,100,0,1)==ESP_ERR_INVALID_STATE);
    CHECK(key_sound_chunk("xxxxxxxxxxxxxxxx",0,(uint8_t*)pcm,200,logic_crc32(0,pcm,200))!=ESP_OK);
    CHECK(key_sound_commit("xxxxxxxxxxxxxxxx")!=ESP_OK);CHECK(key_sound_cancel("0123456789abcdef")==ESP_OK);
    memcpy(before,flash,sizeof(flash));
    for(int failure=0;failure<3;++failure){memcpy(flash,before,sizeof(flash));reboot();upload(1,pcm,32000,false);operations=0;fail_at=failure==2?33:failure;CHECK(key_sound_commit("0123456789abcdef")!=ESP_OK);fail_at=-1;reboot();key_sound_snapshot(&snapshot);CHECK(snapshot.keys[1].crc32==crcs[1]);for(unsigned key=0;key<4;++key)if(key!=1)CHECK(snapshot.keys[key].crc32==crcs[key]);}
    CHECK(key_sound_use_default(1)==ESP_OK);reboot();key_sound_snapshot(&snapshot);CHECK(!snapshot.keys[1].custom&&snapshot.keys[1].ready);CHECK(snapshot.keys[0].custom&&snapshot.keys[2].custom&&snapshot.keys[3].custom);
    puts("PASS: legacy migration, four independent 2-second clips, reboot/CRC, owner/target isolation, retry, partial NOR failure, common fallback, unclipped accumulation and limiter");return 0;
}
