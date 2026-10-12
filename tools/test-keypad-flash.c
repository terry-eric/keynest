#include "../firmware/keyboard/main/flash_store.h"
#include "esp_partition.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); return 1; } } while (0)
static uint8_t storage[0x40000], backup[0x40000];
static esp_partition_t partition = {sizeof(storage)};
static int fail_operation = -1, operations;
static int available = 1;
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label) {
    (void)type; (void)subtype; return available && !strcmp(label,"recording") ? &partition : NULL;
}
esp_err_t esp_partition_read(const esp_partition_t *p,size_t offset,void *out,size_t size) {
    if(offset+size>p->size)return ESP_ERR_INVALID_ARG; memcpy(out,storage+offset,size); return ESP_OK;
}
esp_err_t esp_partition_write(const esp_partition_t *p,size_t offset,const void *src,size_t size) {
    if(offset+size>p->size)return ESP_ERR_INVALID_ARG;
    int fail=operations++==fail_operation; size_t bytes=fail?size/2:size;
    const uint8_t *input=src; for(size_t i=0;i<bytes;i++)storage[offset+i]&=input[i];
    return fail?ESP_FAIL:ESP_OK;
}
esp_err_t esp_partition_erase_range(const esp_partition_t *p,size_t offset,size_t size) {
    if(offset%4096||size%4096||offset+size>p->size)return ESP_ERR_INVALID_ARG;
    int fail=operations++==fail_operation;memset(storage+offset,255,fail?size/2:size);return fail?ESP_FAIL:ESP_OK;
}
static int clip_is(const int16_t *pcm) {
    uint8_t output[44+256]; size_t count=0;
    return flash_store_length()==sizeof(output)&&flash_store_read(0,output,sizeof(output),&count)==ESP_OK&&count==sizeof(output)&&
        !memcmp(output,"RIFF",4)&&!memcmp(output+44,pcm,256);
}
int main(void) {
    int16_t first[128],second[128];for(unsigned i=0;i<128;i++){first[i]=i*31;second[i]=-((int)i*41);}
    memset(storage,255,sizeof(storage));CHECK(flash_store_init()==ESP_OK);CHECK(flash_store_length()==0);
    CHECK(flash_store_save(first,128)==ESP_OK);CHECK(clip_is(first));memcpy(backup,storage,sizeof(storage));
    for(int failure=0;failure<4;failure++) {
        memcpy(storage,backup,sizeof(storage));fail_operation=-1;CHECK(flash_store_init()==ESP_OK);
        operations=0;fail_operation=failure;CHECK(flash_store_save(second,128)==ESP_FAIL);
        fail_operation=-1;CHECK(flash_store_init()==ESP_OK);CHECK(clip_is(first));
    }
    memcpy(storage,backup,sizeof(storage));CHECK(flash_store_init()==ESP_OK);CHECK(flash_store_save(second,128)==ESP_OK);CHECK(clip_is(second));
    CHECK(flash_store_init()==ESP_OK);CHECK(clip_is(second));
    storage[0x20000+0x1000+60]^=1;CHECK(flash_store_init()==ESP_OK);CHECK(clip_is(first));
    available=0;CHECK(flash_store_init()==ESP_ERR_NOT_FOUND);CHECK(flash_store_length()==0);
    puts("PASS: actual Flash store with simulated NOR, alternating slots, reboot, partial erase/header/PCM/commit failure, CRC corruption fallback");
    return 0;
}
