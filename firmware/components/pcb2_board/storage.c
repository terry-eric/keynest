#include "pcb2_board.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static sdmmc_card_t *card;
static bool bus_owned;

esp_err_t pcb2_sd_mount(void) {
    if (card || bus_owned) return ESP_ERR_INVALID_STATE;
    spi_bus_config_t bus = {
        .mosi_io_num = PCB2_PIN_SD_MOSI,
        .miso_io_num = PCB2_PIN_SD_MISO,
        .sclk_io_num = PCB2_PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;
    bus_owned = true;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.gpio_cs = PCB2_PIN_SD_CS;
    device.host_id = SPI2_HOST;
    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &device, &mount, &card);
    if (err != ESP_OK) {
        card = NULL;
        if (spi_bus_free(SPI2_HOST) == ESP_OK) bus_owned = false;
    }
    return err;
}

esp_err_t pcb2_sd_unmount(void) {
    if (!card && !bus_owned) return ESP_ERR_INVALID_STATE;
    if (card) {
        esp_err_t err = esp_vfs_fat_sdcard_unmount("/sdcard", card);
        if (err != ESP_OK) return err;
        card = NULL;
    }
    if (bus_owned) {
        esp_err_t err = spi_bus_free(SPI2_HOST);
        if (err != ESP_OK) return err;
        bus_owned = false;
    }
    return ESP_OK;
}

bool pcb2_sd_is_mounted(void) { return card != NULL; }
