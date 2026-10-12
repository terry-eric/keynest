#include "recorder.h"
#include "flash_store.h"
#include "app_logic.h"
#include "pcb2_board.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

enum { CMD_START, CMD_MOUNT };
#define STREAM_FRAMES 8192U
typedef struct { uint32_t generation; uint16_t count; int16_t pcm[43]; } packet_t;
static QueueHandle_t packets, commands;
static SemaphoreHandle_t mutex;
static recorder_status_t status;
static uint32_t generation;
static bool writer_open;
static unsigned next_number = 1;
static char stream_owner[17];
static int16_t stream[STREAM_FRAMES];
static size_t stream_head, stream_count;
static FILE *download_file;

void recorder_snapshot(recorder_status_t *s) {
    xSemaphoreTake(mutex, portMAX_DELAY); *s = status; xSemaphoreGive(mutex);
}
static void error_message(const char *message) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    status.state = REC_ERROR; snprintf(status.error, sizeof(status.error), "%s", message);
    xSemaphoreGive(mutex);
}
esp_err_t recorder_stop(void) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (status.state == REC_ACTIVE || status.state == REC_STARTING) status.state = REC_STOPPING;
    xSemaphoreGive(mutex); return ESP_OK;
}
esp_err_t recorder_begin(unsigned target, const char *owner) {
    if (target > REC_SD || (target == REC_BROWSER && (!owner || strlen(owner) != 16))) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (status.mounting || status.downloading || writer_open || (status.state != REC_IDLE && status.state != REC_ERROR) ||
        (target == REC_SD && !status.sd_ready) || (target == REC_FLASH && !status.flash_ready)) {
        xSemaphoreGive(mutex); return ESP_ERR_INVALID_STATE;
    }
    status.state = REC_STARTING; status.target = target; status.frames = status.dropped_frames = 0;
    status.error[0] = 0; ++generation; stream_head = stream_count = 0;
    snprintf(stream_owner, sizeof(stream_owner), "%s", target == REC_BROWSER ? owner : "");
    uint8_t command = CMD_START; bool sent = xQueueSend(commands, &command, 0) == pdTRUE;
    if (!sent) status.state = REC_IDLE;
    xSemaphoreGive(mutex); return sent ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t recorder_retry_mount(void) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (status.sd_ready) { xSemaphoreGive(mutex); return ESP_OK; }
    if (status.mounting || writer_open || status.downloading || status.state == REC_STARTING) {
        xSemaphoreGive(mutex); return ESP_ERR_INVALID_STATE;
    }
    uint8_t command = CMD_MOUNT; bool sent = xQueueSend(commands, &command, 0) == pdTRUE;
    if (sent) status.mounting = true;
    xSemaphoreGive(mutex); return sent ? ESP_OK : ESP_ERR_TIMEOUT;
}
void recorder_feed(const int16_t *samples, size_t count) {
    if (!count || count > 43) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool active = status.state == REC_ACTIVE; uint32_t current = generation;
    xSemaphoreGive(mutex); if (!active) return;
    packet_t packet = {.generation = current, .count = count}; memcpy(packet.pcm, samples, count * 2);
    if (xQueueSend(packets, &packet, 0) != pdTRUE) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (generation == current && status.state == REC_ACTIVE) {
            status.dropped_frames += count; status.state = REC_ERROR;
            snprintf(status.error, sizeof(status.error), "Capture queue overflow; recording stopped");
        }
        xSemaphoreGive(mutex);
    }
}
esp_err_t recorder_stream_read(const char *owner, int16_t *buffer, size_t capacity, size_t *count) {
    *count = 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!owner || strcmp(owner, stream_owner) || status.target != REC_BROWSER) {
        xSemaphoreGive(mutex); return ESP_ERR_INVALID_STATE;
    }
    size_t bytes = stream_count; if (bytes > capacity) bytes = capacity;
    for (size_t i = 0; i < bytes; ++i) buffer[i] = stream[(stream_head + i) % STREAM_FRAMES];
    stream_head = (stream_head + bytes) % STREAM_FRAMES; stream_count -= bytes; *count = bytes;
    xSemaphoreGive(mutex); return ESP_OK;
}
esp_err_t recorder_download_begin(char filename[16]) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (writer_open || status.mounting || status.downloading ||
        (status.state != REC_IDLE && status.state != REC_ERROR) || !status.filename[0]) {
        xSemaphoreGive(mutex); return ESP_ERR_INVALID_STATE;
    }
    memcpy(filename, status.filename, 16); status.downloading = true; unsigned target = status.last_target;
    xSemaphoreGive(mutex);
    if (target == REC_SD) {
        char path[40]; snprintf(path, sizeof(path), "/sdcard/%s", filename); download_file = fopen(path, "rb");
        if (!download_file) { recorder_download_end(); return ESP_ERR_NOT_FOUND; }
    }
    return ESP_OK;
}
esp_err_t recorder_download_read(size_t offset, void *buffer, size_t capacity, size_t *count) {
    *count = 0;
    if (download_file) { *count = fread(buffer, 1, capacity, download_file); return ferror(download_file) ? ESP_FAIL : ESP_OK; }
    return flash_store_read(offset, buffer, capacity, count);
}
void recorder_download_end(void) {
    if (download_file) { fclose(download_file); download_file = NULL; }
    xSemaphoreTake(mutex, portMAX_DELAY); status.downloading = false; xSemaphoreGive(mutex);
}
static bool recording_name(const char *name) {
    if (strlen(name) != 12 || strncmp(name, "REC", 3) || strcmp(name + 8, ".WAV")) return false;
    for (unsigned i = 3; i < 8; ++i) if (name[i] < '0' || name[i] > '9') return false;
    return true;
}
static void mount_card(void) {
    if (!pcb2_sd_is_mounted()) pcb2_sd_unmount();
    esp_err_t err = pcb2_sd_is_mounted() ? ESP_OK : pcb2_sd_mount();
    char latest[16] = {0}; next_number = 1;
    if (err == ESP_OK) {
        DIR *dir = opendir("/sdcard");
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir))) {
                if (!recording_name(entry->d_name)) continue;
                unsigned number = 0; sscanf(entry->d_name + 3, "%5u", &number);
                if (number >= next_number) { next_number = number + 1; memcpy(latest, entry->d_name, 13); }
            }
            closedir(dir);
        }
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    status.mounting = false; status.sd_ready = err == ESP_OK;
    // An absent card is optional; browser and internal Flash remain available.
    if (latest[0] && !status.filename[0]) { memcpy(status.filename, latest, 16); status.last_target = REC_SD; }
    xSemaphoreGive(mutex);
}
static FILE *new_file(char name[16]) {
    char path[40];
    while (next_number <= 99999) {
        snprintf(name, 16, "REC%05u.WAV", next_number++); snprintf(path, sizeof(path), "/sdcard/%s", name);
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) { if (errno == EEXIST) continue; return NULL; }
        FILE *file = fdopen(fd, "wb"); if (!file) { close(fd); unlink(path); } return file;
    }
    return NULL;
}
static void worker(void *unused) {
    (void)unused;
    bool flash_ok = flash_store_init() == ESP_OK;
    xSemaphoreTake(mutex, portMAX_DELAY); status.flash_ready = flash_ok;
    if (flash_store_length()) { snprintf(status.filename, 16, "INTERNAL.WAV"); status.last_target = REC_FLASH; }
    xSemaphoreGive(mutex);
    // The SD socket is optional. Mount only on an explicit web request.
    FILE *file = NULL; int16_t *ram = NULL; char name[16] = {0};
    uint32_t written = 0, active_generation = 0; unsigned target = REC_BROWSER; uint64_t started = 0;
    for (;;) {
        uint8_t command;
        while (xQueueReceive(commands, &command, 0) == pdTRUE) {
            if (command == CMD_MOUNT && !writer_open) mount_card();
            if (command != CMD_START || writer_open) continue;
            recorder_status_t snapshot; recorder_snapshot(&snapshot); if (snapshot.state != REC_STARTING) continue;
            xQueueReset(packets); written = 0; target = snapshot.target; started = esp_timer_get_time();
            if (target == REC_FLASH) {
                ram = malloc(FLASH_CAPTURE_FRAMES * sizeof(int16_t));
                if (!ram) { error_message("Not enough RAM for internal Flash recording"); continue; }
            } else if (target == REC_SD) {
                file = new_file(name);
                if (!file) { error_message("Cannot create WAV; check card space"); continue; }
                uint8_t header[44]; logic_wav_header(header, 0);
                if (fwrite(header, 1, 44, file) != 44) { fclose(file); file = NULL; error_message("Cannot write WAV header"); continue; }
            }
            xSemaphoreTake(mutex, portMAX_DELAY); writer_open = true; active_generation = generation;
            if (status.state == REC_STARTING) status.state = REC_ACTIVE;
            xSemaphoreGive(mutex);
        }
        packet_t packet;
        if (xQueueReceive(packets, &packet, pdMS_TO_TICKS(10)) == pdTRUE && writer_open && packet.generation == active_generation) {
            uint32_t limit = target == REC_FLASH ? FLASH_CAPTURE_FRAMES : CAPTURE_RATE * CONFIG_PCB2_RECORD_SECONDS;
            size_t count = packet.count; if (count > limit - written) count = limit - written;
            if (target == REC_FLASH) { memcpy(ram + written, packet.pcm, count * 2); written += count; }
            else if (target == REC_SD) {
                size_t actual = fwrite(packet.pcm, sizeof(int16_t), count, file); written += actual;
                if (actual != count) error_message("SD write failed; partial recording");
            } else {
                xSemaphoreTake(mutex, portMAX_DELAY);
                if (stream_count + count > STREAM_FRAMES) {
                    status.dropped_frames += count; status.state = REC_ERROR;
                    snprintf(status.error, sizeof(status.error), "Browser too slow or disconnected; capture stopped");
                } else {
                    for (size_t i = 0; i < count; ++i) stream[(stream_head + stream_count + i) % STREAM_FRAMES] = packet.pcm[i];
                    stream_count += count; written += count;
                }
                xSemaphoreGive(mutex);
            }
            xSemaphoreTake(mutex, portMAX_DELAY); status.frames = written;
            if (written >= limit && status.state == REC_ACTIVE) status.state = REC_STOPPING;
            xSemaphoreGive(mutex);
        }
        recorder_status_t snapshot; recorder_snapshot(&snapshot);
        if (writer_open && snapshot.state == REC_ACTIVE && esp_timer_get_time() - started >= (int64_t)CONFIG_PCB2_RECORD_SECONDS * 1000000) recorder_stop();
        bool finishing = snapshot.state == REC_STOPPING || snapshot.state == REC_ERROR;
        if (writer_open && finishing && uxQueueMessagesWaiting(packets) == 0) {
            bool valid = true;
            if (target == REC_FLASH) {
                if (written) valid = flash_store_save(ram, written) == ESP_OK;
                free(ram); ram = NULL;
            } else if (target == REC_SD) {
                uint8_t header[44]; logic_wav_header(header, written);
                valid = fflush(file) == 0 && fseek(file, 0, SEEK_SET) == 0 && fwrite(header, 1, 44, file) == 44;
                if (fflush(file) != 0 || fsync(fileno(file)) != 0) valid = false;
                if (fclose(file) != 0) valid = false;
                file = NULL;
            }
            xSemaphoreTake(mutex, portMAX_DELAY); writer_open = false;
            if (valid && written && target != REC_BROWSER) {
                status.last_target = target; snprintf(status.filename, 16, "%s", target == REC_FLASH ? "INTERNAL.WAV" : name);
            }
            if (!valid) snprintf(status.error, sizeof(status.error), "Recording save failed; previous clip retained when possible");
            status.state = valid && !status.error[0] ? REC_IDLE : REC_ERROR;
            xSemaphoreGive(mutex);
        } else if (!writer_open && snapshot.state == REC_STOPPING) {
            xSemaphoreTake(mutex, portMAX_DELAY); status.state = REC_IDLE; xSemaphoreGive(mutex);
        }
    }
}
esp_err_t recorder_start_worker(void) {
    mutex = xSemaphoreCreateMutex(); packets = xQueueCreate(32, sizeof(packet_t)); commands = xQueueCreate(4, sizeof(uint8_t));
    if (!mutex || !packets || !commands) return ESP_ERR_NO_MEM;
    return xTaskCreatePinnedToCore(worker, "keypad-rec", 6144, NULL, 3, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
