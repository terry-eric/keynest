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
#define DATA_OFFSET 0x1000U
#define SOUND_MAGIC 0x534b3250U
#define BUFFER_BYTES 2048U
#define BUFFER_BLOCKS ((KEY_SOUND_MAX_FRAMES*2+BUFFER_BYTES-1)/BUFFER_BYTES)
typedef struct { uint8_t *blocks[BUFFER_BLOCKS]; uint32_t bytes; } sample_buffer_t;
typedef struct { uint32_t magic, sequence, frames, crc; } metadata_t;
static const esp_partition_t *partition;
static SemaphoreHandle_t mutex, operation;
static key_sound_status_t status;
static sample_buffer_t *active, *pending;
static uint32_t positions[4], received, pending_frames, pending_crc;
static unsigned pending_storage;
static char pending_owner[17];
static char committed_owner[17];
static int64_t touched;
static int flash_slot = -1;
static metadata_t flash_meta;

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
static void activate(sample_buffer_t *pcm, uint32_t frames, uint32_t crc, unsigned storage, const char *name) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    sample_buffer_t *old=active; active=pcm; status.ready=true; status.frames=frames;
    status.crc32=crc; status.storage=storage; status.error[0]=0;
    snprintf(status.filename, sizeof(status.filename), "%s", name ? name : "");
    for (unsigned i=0; i<4; ++i) positions[i]=frames*3;
    xSemaphoreGive(mutex); sample_free(old);
}
void key_sound_snapshot(key_sound_status_t *out) {
    memset(out,0,sizeof(*out)); if (!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY); *out=status; xSemaphoreGive(mutex);
}
bool key_sound_busy(void) {
    key_sound_status_t s; key_sound_snapshot(&s); return s.uploading;
}
void key_sound_trigger(unsigned key) {
    if (!mutex || key>=4) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if (status.ready) { positions[key]=0; ++status.plays[key]; }
    xSemaphoreGive(mutex);
}
void key_sound_stop(unsigned key) {
    if (!mutex || key>=4) return;
    xSemaphoreTake(mutex,portMAX_DELAY); positions[key]=status.frames*3; xSemaphoreGive(mutex);
}
void key_sound_mix(int16_t *output, size_t count, unsigned volume) {
    if (!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if (active && status.ready) for (size_t j=0; j<count; ++j) {
        int32_t sum=0;
        for (unsigned i=0; i<4; ++i) {
            uint32_t p=positions[i]; if (p>=status.frames*3) continue;
            uint32_t n=p/3, next=n+1<status.frames?n+1:n;
            int32_t sample=((int32_t)sample_at(active,n)*(int32_t)(3-p%3)+(int32_t)sample_at(active,next)*(int32_t)(p%3))/3;
            uint32_t remain=status.frames*3-p;
            if (p<144) sample=sample*(int32_t)p/144;
            if (remain<144) sample=sample*(int32_t)remain/144;
            sum+=sample; ++positions[i]; ++status.rendered[i];
        }
        // Limit simultaneous voices before applying the speaker volume.
        sum=sum>32767?32767:sum<-32768?-32768:sum;
        sum=output[j]+sum*(int32_t)(volume>100?100:volume)/100;
        output[j]=sum>32767?32767:sum<-32768?-32768:sum;
    }
    xSemaphoreGive(mutex);
}
static bool valid_slot(unsigned slot, metadata_t *m) {
    if (!partition || esp_partition_read(partition,slot*SLOT_SIZE,m,sizeof(*m))!=ESP_OK ||
        m->magic!=SOUND_MAGIC || !m->frames || m->frames>KEY_SOUND_MAX_FRAMES) return false;
    uint8_t block[1024]; uint32_t crc=0; size_t length=m->frames*2;
    for (size_t offset=0; offset<length; offset+=sizeof(block)) {
        size_t count=length-offset; if(count>sizeof(block)) count=sizeof(block);
        if(esp_partition_read(partition,slot*SLOT_SIZE+DATA_OFFSET+offset,block,count)!=ESP_OK) return false;
        crc=logic_crc32(crc,block,count);
    }
    return crc==m->crc;
}
static esp_err_t save_flash(void) {
    if (!partition) return ESP_ERR_NOT_FOUND;
    unsigned slot=flash_slot==0?1:0; size_t base=slot*SLOT_SIZE, length=pending_frames*2;
    metadata_t m={SOUND_MAGIC,flash_slot<0?1:flash_meta.sequence+1,pending_frames,pending_crc};
    esp_err_t err=esp_partition_erase_range(partition,base,DATA_OFFSET+(length+4095)/4096*4096);
    for(size_t offset=0;err==ESP_OK && offset<length;offset+=BUFFER_BYTES) {
        size_t count=length-offset;if(count>BUFFER_BYTES)count=BUFFER_BYTES;
        err=esp_partition_write(partition,base+DATA_OFFSET+offset,pending->blocks[offset/BUFFER_BYTES],count);
    }
    if(err==ESP_OK) err=esp_partition_write(partition,base,&m,sizeof(m));
    metadata_t checked;
    if(err==ESP_OK && !valid_slot(slot,&checked)) err=ESP_FAIL;
    if(err==ESP_OK) { flash_slot=slot; flash_meta=m; }
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
static esp_err_t remember(unsigned storage,const char *name) {
    nvs_handle_t handle; esp_err_t err=nvs_open("pcb2_sound",NVS_READWRITE,&handle);
    if(err!=ESP_OK) return err;
    err=nvs_set_u8(handle,"storage",storage);
    if(err==ESP_OK) err=nvs_set_str(handle,"filename",name?name:"");
    if(err==ESP_OK) err=nvs_set_u32(handle,"crc",pending_crc);
    if(err==ESP_OK) err=nvs_set_u32(handle,"frames",pending_frames);
    if(err==ESP_OK) err=nvs_commit(handle);
    nvs_close(handle); return err;
}
esp_err_t key_sound_begin(const char *owner,unsigned storage,uint32_t frames,uint32_t crc) {
    if(!operation) return ESP_ERR_INVALID_STATE;
    if(!owner || strlen(owner)!=16 || (storage!=REC_FLASH && storage!=REC_SD) ||
       !frames || frames>KEY_SOUND_MAX_FRAMES) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(operation,portMAX_DELAY);
    esp_err_t err=ESP_OK;
    recorder_status_t r; recorder_snapshot(&r);
    if(pending && !strcmp(owner,pending_owner) && storage==pending_storage && frames==pending_frames && crc==pending_crc) {
        touched=esp_timer_get_time();
    }
    else if(pending || !idle() || (storage==REC_SD && !r.sd_ready)) err=ESP_ERR_INVALID_STATE;
    else {
        pending=sample_allocate(frames);
        if(!pending) err=ESP_ERR_NO_MEM;
        else {
            pending_frames=frames; pending_crc=crc; pending_storage=storage; received=0;
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
        if(err==ESP_OK) err=remember(pending_storage,name);
        if(err==ESP_OK) {
            activate(pending,pending_frames,pending_crc,pending_storage,name);
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
static esp_err_t load_sd(const char *name,uint32_t expected_frames,uint32_t expected_crc) {
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
                    activate(pcm,frames,expected_crc,REC_SD,name); pcm=NULL; err=ESP_OK;
                }
            }
        }
    }
    fclose(file); sample_free(pcm); return err;
}
static void service(void *unused) {
    (void)unused; nvs_handle_t handle; uint8_t storage=REC_FLASH; char name[16]="";
    uint32_t expected_frames=0,expected_crc=0;
    if(nvs_open("pcb2_sound",NVS_READONLY,&handle)==ESP_OK) {
        nvs_get_u8(handle,"storage",&storage); size_t length=sizeof(name);
        nvs_get_str(handle,"filename",name,&length); nvs_close(handle);
        if(nvs_open("pcb2_sound",NVS_READONLY,&handle)==ESP_OK) {
            nvs_get_u32(handle,"crc",&expected_crc); nvs_get_u32(handle,"frames",&expected_frames); nvs_close(handle);
        }
    }
    if(storage==REC_SD) {
        // Restore a saved SD sound after boot, without blocking USB startup.
        vTaskDelay(pdMS_TO_TICKS(200)); recorder_retry_mount();
    }
    bool restore=storage==REC_SD;
    for(;;) {
        if(xSemaphoreTake(operation,0)==pdTRUE) {
            if(pending && esp_timer_get_time()-touched>30000000) clear_pending();
            recorder_status_t r; recorder_snapshot(&r);
            if(restore && !pending && idle() && r.sd_ready) {
                set_busy(true);
                esp_err_t err=idle()?load_sd(name,expected_frames,expected_crc):ESP_ERR_INVALID_STATE;
                set_busy(false);if(err==ESP_ERR_INVALID_STATE){xSemaphoreGive(operation);vTaskDelay(pdMS_TO_TICKS(200));continue;}
                restore=false;
                if(err!=ESP_OK) {
                    xSemaphoreTake(mutex,portMAX_DELAY);
                    snprintf(status.error,sizeof(status.error),"SD sound unavailable: %s",esp_err_to_name(err));
                    xSemaphoreGive(mutex);
                }
            }
            // A user upload takes priority over the boot-time SD restoration.
            if(status.ready && !pending) restore=false;
            xSemaphoreGive(operation);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
esp_err_t key_sound_init(void) {
    mutex=xSemaphoreCreateMutex(); operation=xSemaphoreCreateMutex();
    if(!mutex || !operation) return ESP_ERR_NO_MEM;
    partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"key_sound");
    if(partition && partition->size>=2*SLOT_SIZE) for(unsigned i=0;i<2;++i) {
        metadata_t m;
        if(valid_slot(i,&m) && (flash_slot<0 || (int32_t)(m.sequence-flash_meta.sequence)>0)) {
            flash_slot=i; flash_meta=m;
        }
    }
    uint8_t storage=REC_FLASH; nvs_handle_t handle;
    if(nvs_open("pcb2_sound",NVS_READONLY,&handle)==ESP_OK) { nvs_get_u8(handle,"storage",&storage); nvs_close(handle); }
    status.storage=storage;
    if(storage==REC_SD)snprintf(status.error,sizeof(status.error),"Waiting for SD card to restore saved key sound");
    if(storage==REC_FLASH && flash_slot>=0) {
        sample_buffer_t *pcm=sample_allocate(flash_meta.frames);
        bool ok=pcm!=NULL;
        for(uint32_t offset=0;ok && offset<flash_meta.frames*2;offset+=BUFFER_BYTES) {
            uint32_t count=flash_meta.frames*2-offset;if(count>BUFFER_BYTES)count=BUFFER_BYTES;
            ok=esp_partition_read(partition,flash_slot*SLOT_SIZE+DATA_OFFSET+offset,pcm->blocks[offset/BUFFER_BYTES],count)==ESP_OK;
        }
        if(ok)activate(pcm,flash_meta.frames,flash_meta.crc,REC_FLASH,"");
        else sample_free(pcm);
    }
    return xTaskCreatePinnedToCore(service,"keypad-sound",4096,NULL,2,NULL,0)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
