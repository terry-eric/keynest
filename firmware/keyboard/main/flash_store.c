#include "flash_store.h"
#include "app_logic.h"
#include "esp_partition.h"
#include <string.h>

#define SLOT_SIZE 0x20000U
#define DATA_OFFSET 0x1000U
#define MAGIC 0x50433257U
typedef struct { uint32_t magic, sequence, frames, length, crc; } metadata_t;
static const esp_partition_t *partition;
static int active_slot = -1;
static metadata_t active;
static bool slot_valid(unsigned slot, metadata_t *metadata) {
    size_t base = slot * SLOT_SIZE;
    if (esp_partition_read(partition, base, metadata, sizeof(*metadata)) != ESP_OK ||
        metadata->magic != MAGIC || !metadata->frames || metadata->frames > FLASH_CAPTURE_FRAMES ||
        metadata->length != 44 + metadata->frames * 2) return false;
    uint8_t block[1024]; uint32_t crc = 0;
    for (size_t offset = 0; offset < metadata->length; offset += sizeof(block)) {
        size_t count = metadata->length - offset; if (count > sizeof(block)) count = sizeof(block);
        if (esp_partition_read(partition, base + DATA_OFFSET + offset, block, count) != ESP_OK) return false;
        crc = logic_crc32(crc, block, count);
    }
    return crc == metadata->crc;
}
esp_err_t flash_store_init(void) {
    active_slot = -1; memset(&active, 0, sizeof(active));
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "recording");
    if (!partition || partition->size < 2 * SLOT_SIZE) return ESP_ERR_NOT_FOUND;
    for (unsigned i = 0; i < 2; ++i) {
        metadata_t metadata;
        if (slot_valid(i, &metadata) && (active_slot < 0 || (int32_t)(metadata.sequence - active.sequence) > 0)) {
            active_slot = i; active = metadata;
        }
    }
    return ESP_OK;
}
esp_err_t flash_store_save(const int16_t *samples, uint32_t frames) {
    if (!partition) return ESP_ERR_INVALID_STATE;
    if (!samples || !frames || frames > FLASH_CAPTURE_FRAMES) return ESP_ERR_INVALID_ARG;
    uint8_t header[44]; logic_wav_header(header, frames);
    metadata_t metadata = {.magic = MAGIC, .sequence = active_slot < 0 ? 1 : active.sequence + 1,
                           .frames = frames, .length = 44 + frames * 2};
    metadata.crc = logic_crc32(logic_crc32(0, header, sizeof(header)), samples, frames * 2);
    unsigned slot = active_slot == 0 ? 1 : 0;
    size_t base = slot * SLOT_SIZE;
    size_t erase_size = DATA_OFFSET + (metadata.length + 4095) / 4096 * 4096;
    esp_err_t err = esp_partition_erase_range(partition, base, erase_size);
    if (err == ESP_OK) err = esp_partition_write(partition, base + DATA_OFFSET, header, sizeof(header));
    if (err == ESP_OK) err = esp_partition_write(partition, base + DATA_OFFSET + sizeof(header), samples, frames * 2);
    // Commit metadata last. CRC validates data on reboot; the previous slot is retained.
    if (err == ESP_OK) err = esp_partition_write(partition, base, &metadata, sizeof(metadata));
    if (err == ESP_OK) { active_slot = slot; active = metadata; }
    return err;
}
size_t flash_store_length(void) { return active_slot < 0 ? 0 : active.length; }
esp_err_t flash_store_read(size_t offset, void *buffer, size_t capacity, size_t *count) {
    *count = 0;
    if (active_slot < 0) return ESP_ERR_NOT_FOUND;
    if (offset >= active.length) return ESP_OK;
    size_t bytes = active.length - offset; if (bytes > capacity) bytes = capacity;
    esp_err_t err = esp_partition_read(partition, active_slot * SLOT_SIZE + DATA_OFFSET + offset, buffer, bytes);
    if (err == ESP_OK) *count = bytes;
    return err;
}
