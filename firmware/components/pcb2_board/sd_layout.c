// Read-only structural SD diagnostics. No sector writes, format or file reads.
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "sd_protocol_defs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}
static bool signature(const uint8_t *p) { return p[510] == 0x55 && p[511] == 0xaa; }

static void describe(const char *kind, uint64_t lba, const uint8_t *p) {
    const char *type = !memcmp(p + 3, "EXFAT   ", 8) ? "exFAT" :
        !memcmp(p + 3, "NTFS    ", 8) ? "NTFS" :
        !memcmp(p + 82, "FAT32   ", 8) ? "FAT32" :
        !memcmp(p + 54, "FAT16   ", 8) ? "FAT16" :
        !memcmp(p + 54, "FAT12   ", 8) ? "FAT12" : "unrecognized";
    ESP_LOGI("pcb2_sd", "SD META kind=%s lba=%llu signature=%u type=%s jump=0x%02x", kind, (unsigned long long)lba, signature(p), type, p[0]);
    ESP_LOGI("pcb2_sd", "SD BPB lba=%llu bps=%u spc=%u reserved=%u fats=%u root_entries=%u total16=%u total32=%lu fat16=%u fat32=%lu hidden=%lu root_cluster=%lu backup_boot=%u",
        (unsigned long long)lba, le16(p + 11), p[13], le16(p + 14), p[16], le16(p + 17), le16(p + 19),
        (unsigned long)le32(p + 32), le16(p + 22), (unsigned long)le32(p + 36), (unsigned long)le32(p + 28),
        (unsigned long)le32(p + 44), le16(p + 50));
}

static bool read_one(sdmmc_card_t *card, uint64_t lba, uint8_t *data) {
    if (lba >= (uint64_t)card->csd.capacity || lba > UINT32_MAX) return false;
    esp_err_t err = sdmmc_read_sectors(card, data, (size_t)lba, 1);
    if (err != ESP_OK) ESP_LOGE("pcb2_sd", "SD META read lba=%llu error=%s (0x%x)", (unsigned long long)lba, esp_err_to_name(err), (unsigned)err);
    return err == ESP_OK;
}

// CMD9 only reads the CSD capacity register; it contains no file data or card serial.
static void inspect_csd(sdmmc_card_t *card, uint8_t *buffer) {
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        memset(buffer, 0xa5, 512);
        sdmmc_command_t cmd = {
            .opcode = MMC_SEND_CSD, .arg = 0,
            .flags = SCF_CMD_READ | SCF_CMD_ADTC | SCF_RSP_R1,
            .data = buffer, .datalen = 16, .buflen = 512, .blklen = 16,
            .timeout_ms = 1000,
        };
        esp_err_t err = card->host.do_transaction(card->host.slot, &cmd);
        if (err == ESP_OK) err = cmd.error;
        if (err != ESP_OK) {
            ESP_LOGE("pcb2_sd", "SD CSD read=%u error=%s", attempt + 1, esp_err_to_name(err));
            continue;
        }
        char hex[33];
        static const char digits[] = "0123456789abcdef";
        for (unsigned i = 0; i < 16; ++i) {
            hex[2 * i] = digits[buffer[i] >> 4]; hex[2 * i + 1] = digits[buffer[i] & 15];
        }
        hex[32] = 0;
        ESP_LOGI("pcb2_sd", "SD CSD read=%u raw=%s structure=%u end_bit=%u", attempt + 1, hex, buffer[0] >> 6, buffer[15] & 1);
    }
}

void pcb2_sd_read_layout(const sdspi_device_config_t *device, uint32_t max_khz) {
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = max_khz;
    sdspi_dev_handle_t handle;
    esp_err_t err = host.init();
    if (err != ESP_OK) return;
    err = sdspi_host_init_device(device, &handle);
    if (err != ESP_OK) { ESP_LOGE("pcb2_sd", "SD META device_init=%s", esp_err_to_name(err)); return; }
    host.slot = handle;
    sdmmc_card_t probe = {0};
    uint8_t *block = NULL, *again = NULL;
    err = sdmmc_card_init(&host, &probe);
    if (err != ESP_OK) { ESP_LOGE("pcb2_sd", "SD META card_init=%s", esp_err_to_name(err)); goto done; }
    ESP_LOGI("pcb2_sd", "SD META capacity_bytes=%llu sectors=%d sector_size=%d real_khz=%d",
        (unsigned long long)probe.csd.capacity * probe.csd.sector_size, probe.csd.capacity, probe.csd.sector_size, probe.real_freq_khz);
    ESP_LOGI("pcb2_sd", "SD META ocr=0x%08lx csd_ver=%d block_len=%d is_mmc=%u is_mem=%u", (unsigned long)probe.ocr, probe.csd.csd_ver, probe.csd.read_block_len, probe.is_mmc, probe.is_mem);
    if (probe.csd.sector_size != 512) goto done;
    block = heap_caps_malloc(512, MALLOC_CAP_DMA);
    again = heap_caps_malloc(512, MALLOC_CAP_DMA);
    if (!block || !again) goto done;
    inspect_csd(&probe, block);
    memset(block, 0xa5, 512); memset(again, 0x5a, 512);
    if (!read_one(&probe, 0, block)) goto done;
    if (!read_one(&probe, 0, again)) goto done;
    ESP_LOGI("pcb2_sd", "SD META sector0_repeat_equal=%u", memcmp(block, again, 512) == 0);
    bool all_zero = true, all_ff = true;
    for (unsigned i = 0; i < 512; ++i) { all_zero &= block[i] == 0; all_ff &= block[i] == 0xff; }
    ESP_LOGI("pcb2_sd", "SD META sector0_all_zero=%u sector0_all_ff=%u", all_zero, all_ff);
    describe("sector0", 0, block);
    if (!signature(block)) goto done;
    struct { uint8_t type; uint32_t start, count; } parts[4];
    bool protective = false;
    for (unsigned i = 0; i < 4; ++i) {
        const uint8_t *p = block + 446 + i * 16;
        parts[i].type = p[4]; parts[i].start = le32(p + 8); parts[i].count = le32(p + 12);
        ESP_LOGI("pcb2_sd", "SD PART index=%u type=0x%02x start=%lu count=%lu", i + 1, parts[i].type, (unsigned long)parts[i].start, (unsigned long)parts[i].count);
        protective |= parts[i].type == 0xee;
    }
    if (protective) {
        if (!read_one(&probe, 1, block)) goto done;
        bool gpt = !memcmp(block, "EFI PART", 8);
        uint64_t table = le64(block + 72);
        uint32_t entries = le32(block + 80), entry_size = le32(block + 84);
        ESP_LOGI("pcb2_sd", "SD GPT signature=%u table_lba=%llu entries=%lu entry_size=%lu", gpt, (unsigned long long)table, (unsigned long)entries, (unsigned long)entry_size);
        if (!gpt || !table || table >= (uint64_t)probe.csd.capacity || entry_size != 128 || !entries || entries > 128) goto done;
        static const uint8_t basic[16] = {0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7};
        unsigned checked = entries < 32 ? entries : 32;
        for (unsigned i = 0; i < checked; ++i) {
            if (i % 4 == 0 && !read_one(&probe, table + i / 4, block)) break;
            const uint8_t *p = block + (i % 4) * 128;
            if (memcmp(p, basic, 16)) continue;
            uint64_t first = le64(p + 32), last = le64(p + 40);
            ESP_LOGI("pcb2_sd", "SD GPT basic_index=%u first=%llu last=%llu", i + 1, (unsigned long long)first, (unsigned long long)last);
            if (first && first <= last && last < (uint64_t)probe.csd.capacity && read_one(&probe, first, again)) describe("gpt_volume", first, again);
        }
    } else {
        for (unsigned i = 0; i < 4; ++i) {
            if (!parts[i].type || !parts[i].start || !parts[i].count ||
                (uint64_t)parts[i].start + parts[i].count > (uint64_t)probe.csd.capacity) continue;
            if (!read_one(&probe, parts[i].start, block)) continue;
            describe("mbr_volume", parts[i].start, block);
            if (!memcmp(block + 82, "FAT32   ", 8) && le16(block + 50) &&
                le16(block + 50) < le16(block + 14) && le16(block + 50) < parts[i].count &&
                read_one(&probe, (uint64_t)parts[i].start + le16(block + 50), again))
                describe("fat32_backup", (uint64_t)parts[i].start + le16(block + 50), again);
        }
    }
done:
    free(block); free(again);
    err = sdspi_host_remove_device(handle);
    if (err != ESP_OK) ESP_LOGE("pcb2_sd", "SD META cleanup=%s", esp_err_to_name(err));
}
