#include "app.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

static const uint8_t report_descriptor[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_HID_DESCRIPTOR(2, 5, HID_ITF_PROTOCOL_KEYBOARD, sizeof(report_descriptor), 0x83, 16, 5)
};
static char serial[16];
static const char *strings[] = {(const char[]){0x09, 0x04}, "PCB2", "PCB2 Keypad", serial, "Keypad log", "Four-key keyboard"};
static keypad_report_t last_report;
static portMUX_TYPE report_lock = portMUX_INITIALIZER_UNLOCKED;
static bool last_valid;
static uint64_t last_sent;

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { (void)instance; return report_descriptor; }
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t *buffer, uint16_t length) {
    (void)instance;
    if (type != HID_REPORT_TYPE_INPUT || report_id || !buffer) return 0;
    if (length > sizeof(last_report)) length = sizeof(last_report);
    portENTER_CRITICAL(&report_lock); memcpy(buffer, &last_report, length); portEXIT_CRITICAL(&report_lock);
    return length;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, const uint8_t *buffer, uint16_t size) {
    (void)instance; (void)report_id; (void)type; (void)buffer; (void)size;
}
bool usb_keyboard_connected(void) { return tud_mounted(); }
void usb_keyboard_tick(uint8_t down, const keypad_config_t *config) {
    if (!tud_mounted()) { last_valid = false; return; }
    keypad_report_t report; logic_hid_report(down, config, &report);
    uint64_t now = esp_timer_get_time() / 1000;
    if (last_valid && !memcmp(&report, &last_report, sizeof(report)) && now - last_sent < 500) return;
    if (tud_hid_ready() && tud_hid_keyboard_report(0, report.modifiers, report.keys)) {
        portENTER_CRITICAL(&report_lock); last_report = report; portEXIT_CRITICAL(&report_lock);
        last_valid = true; last_sent = now;
    }
}
esp_err_t usb_keyboard_start(void) {
    uint8_t mac[6]; esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const tinyusb_config_t config = {
        .device_descriptor = NULL, .string_descriptor = strings,
        .string_descriptor_count = sizeof(strings) / sizeof(strings[0]), .external_phy = false,
        .configuration_descriptor = configuration
    };
    err = tinyusb_driver_install(&config);
    if (err != ESP_OK) return err;
    tinyusb_config_cdcacm_t cdc = {.usb_dev = TINYUSB_USBDEV_0, .cdc_port = TINYUSB_CDC_ACM_0, .rx_unread_buf_sz = 64};
    err = tusb_cdc_acm_init(&cdc);
    if (err == ESP_OK) err = esp_tusb_init_console(TINYUSB_CDC_ACM_0);
    return err;
}
