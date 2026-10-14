#include "key_sound.h"
#include "app_logic.h"
#include "recorder.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define SLOT_SIZE 0x20000U
#define COMMON_BANK 4U
#define PER_KEY_BYTES 0x140000U
#define DATA_OFFSET 0x1000U
#define SOUND_MAGIC 0x534b3250U
#define BUFFER_BYTES 2048U
#define BUFFER_BLOCKS ((KEY_SOUND_MAX_FRAMES*2+BUFFER_BYTES-1)/BUFFER_BYTES)
typedef struct { uint8_t *blocks[BUFFER_BLOCKS]; uint32_t bytes; } sample_buffer_t;
typedef struct { uint32_t magic, sequence, frames, crc; } metadata_t;
static const esp_partition_t *partition;
static SemaphoreHandle_t mutex, operation;
static key_sound_status_t status;
typedef struct {
    sample_buffer_t *pcm;
    key_sound_clip_t clip;
    int flash_slot;
    metadata_t meta;
} sound_bank_t;
static sound_bank_t banks[5];
static sample_buffer_t *pending;
static unsigned pending_bank=COMMON_BANK;
static uint32_t positions[4], received, pending_frames, pending_crc;
static unsigned pending_storage;
static char pending_owner[17];
static char committed_owner[17];
static int64_t touched;
static size_t slot_base(unsigned bank,unsigned slot) {
    return (bank==COMMON_BANK?0:2*SLOT_SIZE+bank*2*SLOT_SIZE)+slot*SLOT_SIZE;
}
static sound_bank_t *voice_bank(unsigned key) {
    return banks[key].clip.ready?&banks[key]:&banks[COMMON_BANK];
}

static void sample_free(sample_buffer_t *buffer) {
    if(!buffer)return;
    for(unsigned i=0;i<BUFFER_BLOCKS;++i)free(buffer->blocks[i]);
    free(buffer);
}
static sample_buffer_t *sample_allocate(uint32_t frames) {
    sample_buffer_t *buffer=calloc(1,sizeof(*buffer));
    if(!buffer)return NULL;
    buffer->bytes=frames*2;
    for(uint32_t offset=0;offset<buffer->bytes;offset+=BUFFER_BYTES) {
        uint32_t count=buffer->bytes-offset;if(count>BUFFER_BYTES)count=BUFFER_BYTES;
        buffer->blocks[offset/BUFFER_BYTES]=heap_caps_malloc(count,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!buffer->blocks[offset/BUFFER_BYTES])buffer->blocks[offset/BUFFER_BYTES]=malloc(count);
        if(!buffer->blocks[offset/BUFFER_BYTES]){sample_free(buffer);return NULL;}
    }
    return buffer;
}
static int16_t sample_at(const sample_buffer_t *buffer,uint32_t index) {
    return ((const int16_t*)buffer->blocks[index/(BUFFER_BYTES/2)])[index%(BUFFER_BYTES/2)];
}
static void sample_copy(sample_buffer_t *buffer,uint32_t offset,const uint8_t *bytes,size_t count) {
    while(count) {
        uint32_t within=offset%BUFFER_BYTES;
        size_t part=BUFFER_BYTES-within;if(part>count)part=count;
        memcpy(buffer->blocks[offset/BUFFER_BYTES]+within,bytes,part);
        offset+=part;bytes+=part;count-=part;
    }
}
static uint32_t sample_crc(const sample_buffer_t *buffer) {
    uint32_t crc=0;
    for(uint32_t offset=0;offset<buffer->bytes;offset+=BUFFER_BYTES) {
        uint32_t count=buffer->bytes-offset;if(count>BUFFER_BYTES)count=BUFFER_BYTES;
        crc=logic_crc32(crc,buffer->blocks[offset/BUFFER_BYTES],count);
    }
    return crc;
}
static bool sample_equal(const sample_buffer_t *buffer,uint32_t offset,const uint8_t *bytes,size_t count) {
    while(count) {
        uint32_t within=offset%BUFFER_BYTES;
        size_t part=BUFFER_BYTES-within;if(part>count)part=count;
        if(memcmp(buffer->blocks[offset/BUFFER_BYTES]+within,bytes,part))return false;
        offset+=part;bytes+=part;count-=part;
    }
    return true;
}

static uint16_t le16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)le16(p) | ((uint32_t)le16(p+2) << 16); }
static bool idle(void) {
    recorder_status_t r; recorder_snapshot(&r);
    return (r.state == REC_IDLE || r.state == REC_ERROR) && !r.mounting && !r.downloading;
}
static bool valid_name(const char *name) {
    if (!name || strlen(name) != 12 || strncmp(name, "KEY", 3) || strcmp(name+8, ".WAV")) return false;
    for (unsigned i=3; i<8; ++i) if (name[i]<'0' || name[i]>'9') return false;
    return true;
}
static void set_busy(bool value) {
    xSemaphoreTake(mutex, portMAX_DELAY); status.uploading=value; xSemaphoreGive(mutex);
}
static void clear_pending(void) {
    sample_free(pending); pending=NULL; received=0; pending_owner[0]=0; set_busy(false);
}
static void activate(unsigned bank,sample_buffer_t *pcm,uint32_t frames,uint32_t crc,unsigned storage,const char *name) {
    char filename[16];snprintf(filename,sizeof(filename),"%s",name?name:"");
    xSemaphoreTake(mutex,portMAX_DELAY);
    sound_bank_t *b=&banks[bank]; sample_buffer_t *old=b->pcm; b->pcm=pcm;
    b->clip.ready=true; b->clip.custom=bank!=COMMON_BANK;
    b->clip.frames=frames; b->clip.crc32=crc; b->clip.storage=storage; b->clip.error[0]=0;
    snprintf(b->clip.filename,sizeof(b->clip.filename),"%s",filename);
    for(unsigned i=0;i<4;++i)if(bank==i || (bank==COMMON_BANK&&!banks[i].clip.ready))positions[i]=UINT32_MAX;
    xSemaphoreGive(mutex); sample_free(old);
}
void key_sound_snapshot(key_sound_status_t *out) {
    memset(out,0,sizeof(*out)); if(!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY); *out=status;
    key_sound_clip_t *common=&banks[COMMON_BANK].clip;
    out->ready=common->ready;out->frames=common->frames;out->crc32=common->crc32;out->storage=common->storage;
    snprintf(out->filename,sizeof(out->filename),"%s",common->filename);
    snprintf(out->error,sizeof(out->error),"%s",common->error);
    for(unsigned i=0;i<4;++i) {
        out->keys[i]=voice_bank(i)->clip;
        // Preserve a restoration error for the selected key even while falling back.
        if(banks[i].clip.error[0])snprintf(out->keys[i].error,sizeof(out->keys[i].error),"%s",banks[i].clip.error);
        out->ready|=out->keys[i].ready;
    }
    xSemaphoreGive(mutex);
}
bool key_sound_busy(void) {
    key_sound_status_t s;key_sound_snapshot(&s);return s.uploading;
}
void key_sound_trigger(unsigned key) {
    if(!mutex || key>=4)return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(voice_bank(key)->clip.ready){positions[key]=0;++status.plays[key];}
    xSemaphoreGive(mutex);
}
void key_sound_stop(unsigned key) {
    if(!mutex || key>=4)return;
    xSemaphoreTake(mutex,portMAX_DELAY);positions[key]=UINT32_MAX;xSemaphoreGive(mutex);
}
void key_sound_mix(int32_t *output,size_t count,unsigned volume) {
    if(!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    for(size_t j=0;j<count;++j) {
        int32_t sum=0;
        for(unsigned i=0;i<4;++i) {
            sound_bank_t *b=voice_bank(i);uint32_t p=positions[i],frames=b->clip.frames;
            if(!b->pcm || !b->clip.ready || p>=frames*3)continue;
            uint32_t n=p/3,next=n+1<frames?n+1:n;
            int32_t sample=((int32_t)sample_at(b->pcm,n)*(int32_t)(3-p%3)+(int32_t)sample_at(b->pcm,next)*(int32_t)(p%3))/3;
            uint32_t remain=frames*3-p;
            if(p<144)sample=sample*(int32_t)p/144;
            if(remain<144)sample=sample*(int32_t)remain/144;
            sum+=sample;++positions[i];++status.rendered[i];
        }
        // Accumulate without clipping; the final shared limiter handles all voices.
        output[j]+=sum*(int32_t)(volume>100?100:volume)/100;
    }
    xSemaphoreGive(mutex);
}
static bool valid_slot(unsigned bank,unsigned slot,metadata_t *m) {
    if (!partition || esp_partition_read(partition,slot_base(bank,slot),m,sizeof(*m))!=ESP_OK ||
        m->magic!=SOUND_MAGIC || !m->frames || m->frames>KEY_SOUND_MAX_FRAMES) return false;
    uint8_t block[1024]; uint32_t crc=0; size_t length=m->frames*2;
    for (size_t offset=0; offset<length; offset+=sizeof(block)) {
        size_t count=length-offset; if(count>sizeof(block)) count=sizeof(block);
        if(esp_partition_read(partition,slot_base(bank,slot)+DATA_OFFSET+offset,block,count)!=ESP_OK) return false;
        crc=logic_crc32(crc,block,count);
    }
    return crc==m->crc;
}
static esp_err_t save_flash(void) {
    if (!partition) return ESP_ERR_NOT_FOUND;
    sound_bank_t *b=&banks[pending_bank];
    unsigned slot=b->flash_slot==0?1:0;size_t base=slot_base(pending_bank,slot),length=pending_frames*2;
    metadata_t m={SOUND_MAGIC,b->flash_slot<0?1:b->meta.sequence+1,pending_frames,pending_crc};
    esp_err_t err=esp_partition_erase_range(partition,base,DATA_OFFSET+(length+4095)/4096*4096);
    for(size_t offset=0;err==ESP_OK && offset<length;offset+=BUFFER_BYTES) {
        size_t count=length-offset;if(count>BUFFER_BYTES)count=BUFFER_BYTES;
        err=esp_partition_write(partition,base+DATA_OFFSET+offset,pending->blocks[offset/BUFFER_BYTES],count);
    }
    if(err==ESP_OK) err=esp_partition_write(partition,base,&m,sizeof(m));
    metadata_t checked;
    if(err==ESP_OK && !valid_slot(pending_bank,slot,&checked)) err=ESP_FAIL;
    if(err==ESP_OK) { b->flash_slot=slot;b->meta=m; }
    return err;
}
static esp_err_t save_sd(char name[16]) {
    recorder_status_t r; recorder_snapshot(&r); if(!r.sd_ready) return ESP_ERR_INVALID_STATE;
    char path[40]; int fd=-1;
    for(unsigned i=1;i<=99999;++i) {
        snprintf(name,16,"KEY%05u.WAV",i); snprintf(path,sizeof(path),"/sdcard/%s",name);
        fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0666);
        if(fd>=0) break;
        if(errno!=EEXIST) return ESP_FAIL;
    }
    if(fd<0) return ESP_FAIL;
    FILE *file=fdopen(fd,"wb");
    if(!file) { close(fd); unlink(path); return ESP_FAIL; }
    uint8_t header[44]; logic_wav_header(header,pending_frames);
    bool ok=fwrite(header,1,44,file)==44;
    for(uint32_t offset=0;ok && offset<pending->bytes;offset+=BUFFER_BYTES) {
        uint32_t count=pending->bytes-offset;if(count>BUFFER_BYTES)count=BUFFER_BYTES;
        ok=fwrite(pending->blocks[offset/BUFFER_BYTES],1,count,file)==count;
    }
    if(fflush(file)!=0 || fsync(fileno(file))!=0) ok=false;
    if(fclose(file)!=0) ok=false;
    if(!ok) { unlink(path); return ESP_FAIL; }
    return ESP_OK;
}
static void nvs_name(char out[16],const char *base,unsigned bank) {
    if(bank==COMMON_BANK)snprintf(out,16,"%s",base);
    else snprintf(out,16,"%s%u",base,bank);
}
static esp_err_t remember(unsigned bank,unsigned storage,const char *name,uint32_t frames,uint32_t crc) {
    nvs_handle_t handle;char field[16];esp_err_t err=nvs_open("pcb2_sound",NVS_READWRITE,&handle);
    if(err!=ESP_OK)return err;
    nvs_name(field,"storage",bank);err=nvs_set_u8(handle,field,storage);
    nvs_name(field,"filename",bank);if(err==ESP_OK)err=nvs_set_str(handle,field,name?name:"");
    nvs_name(field,"crc",bank);if(err==ESP_OK)err=nvs_set_u32(handle,field,crc);
    nvs_name(field,"frames",bank);if(err==ESP_OK)err=nvs_set_u32(handle,field,frames);
    if(err==ESP_OK)err=nvs_commit(handle);
    nvs_close(handle);return err;
}
esp_err_t key_sound_use_default(unsigned key) {
    if(!operation)return ESP_ERR_INVALID_STATE;
    if(key>=4)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(operation,portMAX_DELAY);
    esp_err_t err=ESP_ERR_INVALID_STATE;
    if(!pending && idle()) {
        set_busy(true);err=remember(key,REC_BROWSER,"",0,0);
        if(err==ESP_OK) {
            xSemaphoreTake(mutex,portMAX_DELAY);sample_buffer_t *old=banks[key].pcm;
            banks[key].pcm=NULL;memset(&banks[key].clip,0,sizeof(banks[key].clip));positions[key]=UINT32_MAX;
            xSemaphoreGive(mutex);sample_free(old);
        }
        set_busy(false);
    }
    xSemaphoreGive(operation);return err;
}
esp_err_t key_sound_begin(const char *owner,unsigned storage,uint32_t frames,uint32_t crc,int key) {
    if(!operation) return ESP_ERR_INVALID_STATE;
    if(!owner || strlen(owner)!=16 || (storage!=REC_FLASH && storage!=REC_SD) ||
       !frames || frames>KEY_SOUND_MAX_FRAMES || key< -1 || key>3) return ESP_ERR_INVALID_ARG;
    unsigned bank=key<0?COMMON_BANK:(unsigned)key;
    if(bank!=COMMON_BANK && !status.per_key)return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(operation,portMAX_DELAY);
    esp_err_t err=ESP_OK;
    recorder_status_t r; recorder_snapshot(&r);
    if(pending && !strcmp(owner,pending_owner) && bank==pending_bank && storage==pending_storage && frames==pending_frames && crc==pending_crc) {
        touched=esp_timer_get_time();
    }
    else if(pending || !idle() || (storage==REC_SD && !r.sd_ready)) err=ESP_ERR_INVALID_STATE;
    else {
        pending=sample_allocate(frames);
        if(!pending) err=ESP_ERR_NO_MEM;
        else {
            pending_bank=bank;pending_frames=frames; pending_crc=crc; pending_storage=storage; received=0;
            memcpy(pending_owner,owner,17); touched=esp_timer_get_time(); set_busy(true);
        }
    }
    xSemaphoreGive(operation); return err;
}
esp_err_t key_sound_chunk(const char *owner,uint32_t offset,const uint8_t *bytes,size_t count,uint32_t crc) {
    if(!operation) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(operation,portMAX_DELAY);
    esp_err_t err=ESP_ERR_INVALID_ARG;
    if(pending && owner && bytes && !strcmp(owner,pending_owner) && count && count<=1024 &&
       !(count&1) && logic_crc32(0,bytes,count)!=crc)err=ESP_ERR_INVALID_CRC;
    if(pending && owner && bytes && !strcmp(owner,pending_owner) && count && count<=1024 &&
       !(count&1) && logic_crc32(0,bytes,count)==crc) {
        if(offset==received && count<=pending_frames*2-received) {
            sample_copy(pending,received,bytes,count);received+=count;err=ESP_OK;
        } else if(offset<=received && count<=received-offset && sample_equal(pending,offset,bytes,count)) {
            err=ESP_OK; // A lost acknowledgement may safely repeat identical PCM.
        }
        if(err==ESP_OK)touched=esp_timer_get_time();
    }
    if(err!=ESP_OK)ESP_LOGW("key_sound","Chunk rejected offset=%lu received=%lu count=%u pending=%u owner_match=%u crc_match=%u got_crc=%lu requested_crc=%lu",
        (unsigned long)offset,(unsigned long)received,(unsigned)count,pending!=NULL,
        pending && owner && !strcmp(owner,pending_owner),bytes && logic_crc32(0,bytes,count)==crc,
        (unsigned long)(bytes?logic_crc32(0,bytes,count):0),(unsigned long)crc);
    xSemaphoreGive(operation); return err;
}
esp_err_t key_sound_commit(const char *owner) {
    if(!operation) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(operation,portMAX_DELAY);
    esp_err_t err=ESP_ERR_INVALID_STATE; char name[16]="";
    if(!pending && owner && committed_owner[0] && !strcmp(owner,committed_owner))err=ESP_OK;
    if(pending && owner && !strcmp(owner,pending_owner) && idle() &&
       received==pending_frames*2 && sample_crc(pending)==pending_crc) {
        err=pending_storage==REC_FLASH?save_flash():save_sd(name);
        if(err==ESP_OK) err=remember(pending_bank,pending_storage,name,pending_frames,pending_crc);
        if(err==ESP_OK) {
            activate(pending_bank,pending,pending_frames,pending_crc,pending_storage,name);
            memcpy(committed_owner,pending_owner,sizeof(committed_owner));
            pending=NULL; clear_pending();
        }
    }
    xSemaphoreGive(operation); return err;
}
esp_err_t key_sound_cancel(const char *owner) {
    if(!operation) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(operation,portMAX_DELAY);
    esp_err_t err=ESP_ERR_INVALID_STATE;
    if(pending && owner && !strcmp(owner,pending_owner)) { clear_pending(); err=ESP_OK; }
    xSemaphoreGive(operation); return err;
}
static esp_err_t load_sd(unsigned bank,const char *name,uint32_t expected_frames,uint32_t expected_crc) {
    if(!valid_name(name)) return ESP_ERR_INVALID_ARG;
    char path[40]; snprintf(path,sizeof(path),"/sdcard/%s",name);
    FILE *file=fopen(path,"rb"); if(!file) return ESP_ERR_NOT_FOUND;
    uint8_t header[44]; esp_err_t err=ESP_ERR_INVALID_RESPONSE; sample_buffer_t *pcm=NULL;
    if(fread(header,1,44,file)==44 && !memcmp(header,"RIFF",4) && !memcmp(header+8,"WAVEfmt ",8) &&
       le32(header+16)==16 && le16(header+20)==1 && le16(header+22)==1 &&
       le32(header+24)==CAPTURE_RATE && le16(header+32)==2 && le16(header+34)==16 &&
       !memcmp(header+36,"data",4)) {
        uint32_t bytes=le32(header+40), frames=bytes/2;
        if(bytes && !(bytes&1) && frames<=KEY_SOUND_MAX_FRAMES && le32(header+4)==bytes+36) {
            pcm=sample_allocate(frames);
            if(!pcm) err=ESP_ERR_NO_MEM;
            else {
                bool ok=true;
                for(uint32_t offset=0;ok && offset<bytes;offset+=BUFFER_BYTES) {
                    uint32_t count=bytes-offset;if(count>BUFFER_BYTES)count=BUFFER_BYTES;
                    ok=fread(pcm->blocks[offset/BUFFER_BYTES],1,count,file)==count;
                }
                if(ok && fgetc(file)==EOF && !ferror(file) && frames==expected_frames && sample_crc(pcm)==expected_crc) {
                    activate(bank,pcm,frames,expected_crc,REC_SD,name); pcm=NULL; err=ESP_OK;
                }
            }
        }
    }
    fclose(file); sample_free(pcm); return err;
}
static void service(void *unused) {
    (void)unused;bool restore[5]={0},mount=false;
    for(unsigned i=0;i<5;++i){restore[i]=banks[i].clip.storage==REC_SD;mount|=restore[i];}
    if(mount){vTaskDelay(pdMS_TO_TICKS(200));recorder_retry_mount();}
    for(;;) {
        if(xSemaphoreTake(operation,0)==pdTRUE) {
            if(pending && esp_timer_get_time()-touched>30000000)clear_pending();
            recorder_status_t r;recorder_snapshot(&r);
            for(unsigned i=0;i<5;++i) {
                sound_bank_t *b=&banks[i];
                // A new upload or reset wins over boot-time SD restoration.
                if(b->clip.ready || b->clip.storage!=REC_SD)restore[i]=false;
                if(restore[i] && !pending && idle() && r.sd_ready) {
                    set_busy(true);
                    esp_err_t err=idle()?load_sd(i,b->clip.filename,b->clip.frames,b->clip.crc32):ESP_ERR_INVALID_STATE;
                    set_busy(false);
                    if(err!=ESP_ERR_INVALID_STATE) {
                        restore[i]=false;
                        if(err!=ESP_OK){xSemaphoreTake(mutex,portMAX_DELAY);snprintf(b->clip.error,sizeof(b->clip.error),"SD sound unavailable: %s",esp_err_to_name(err));xSemaphoreGive(mutex);}
                    }
                }
            }
            xSemaphoreGive(operation);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
esp_err_t key_sound_init(void) {
    mutex=xSemaphoreCreateMutex();operation=xSemaphoreCreateMutex();
    if(!mutex || !operation)return ESP_ERR_NO_MEM;
    partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"key_sound");
    status.per_key=partition && partition->size>=PER_KEY_BYTES;
    for(unsigned bank=0;bank<5;++bank) {
        sound_bank_t *b=&banks[bank];b->flash_slot=-1;positions[bank%4]=UINT32_MAX;
        if(bank!=COMMON_BANK && !status.per_key)continue;
        if(partition && partition->size>=2*SLOT_SIZE)for(unsigned slot=0;slot<2;++slot) {
            metadata_t m;
            if(valid_slot(bank,slot,&m) && (b->flash_slot<0 || (int32_t)(m.sequence-b->meta.sequence)>0)){b->flash_slot=slot;b->meta=m;}
        }
        b->clip.storage=bank==COMMON_BANK?REC_FLASH:REC_BROWSER;
        nvs_handle_t handle;char field[16];uint8_t storage=b->clip.storage;
        if(nvs_open("pcb2_sound",NVS_READONLY,&handle)==ESP_OK) {
            nvs_name(field,"storage",bank);nvs_get_u8(handle,field,&storage);b->clip.storage=storage;
            nvs_name(field,"filename",bank);size_t length=sizeof(b->clip.filename);nvs_get_str(handle,field,b->clip.filename,&length);
            nvs_name(field,"frames",bank);nvs_get_u32(handle,field,&b->clip.frames);
            nvs_name(field,"crc",bank);nvs_get_u32(handle,field,&b->clip.crc32);nvs_close(handle);
        }
        if(storage==REC_SD)snprintf(b->clip.error,sizeof(b->clip.error),"Waiting for SD card to restore saved key sound");
        if(storage==REC_FLASH && b->flash_slot>=0) {
            sample_buffer_t *pcm=sample_allocate(b->meta.frames);bool ok=pcm!=NULL;
            for(uint32_t offset=0;ok && offset<b->meta.frames*2;offset+=BUFFER_BYTES) {
                uint32_t count=b->meta.frames*2-offset;if(count>BUFFER_BYTES)count=BUFFER_BYTES;
                ok=esp_partition_read(partition,slot_base(bank,b->flash_slot)+DATA_OFFSET+offset,pcm->blocks[offset/BUFFER_BYTES],count)==ESP_OK;
            }
            if(ok)activate(bank,pcm,b->meta.frames,b->meta.crc,REC_FLASH,"");else sample_free(pcm);
        }
    }
    return xTaskCreatePinnedToCore(service,"keypad-sound",4096,NULL,2,NULL,0)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
