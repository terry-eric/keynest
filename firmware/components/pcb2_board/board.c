#include "pcb2_board.h"
#include "board_logic.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include <string.h>

static const gpio_num_t key_pins[PCB2_KEY_COUNT] = {
    PCB2_PIN_KEY1, PCB2_PIN_KEY2, PCB2_PIN_KEY3, PCB2_PIN_KEY4
};
static pcb2_debounce_t keys[PCB2_KEY_COUNT];
static uint8_t colors[PCB2_LED_COUNT][3];
static uint8_t brightness = 32;
static rmt_channel_handle_t strip;
static rmt_encoder_handle_t encoder;
static bool strip_enabled;
static rmt_symbol_word_t symbols[PCB2_LED_COUNT * 24 + 1];

// Worldsemi WS2812B-2020-V6 V1.4, page 4. 20MHz = 50ns per tick.
// T0H/T0L=300/950ns, T1H/T1L=650/600ns, bit=1250ns, reset=350us.
enum { ZERO_HIGH = 6, ZERO_LOW = 19, ONE_HIGH = 13, ONE_LOW = 12, RESET_HALF = 3500 };
_Static_assert(ZERO_HIGH + ZERO_LOW == 25 && ONE_HIGH + ONE_LOW == 25, "800kHz bit period");

esp_err_t pcb2_board_init(void) {
    if (strip) return ESP_ERR_INVALID_STATE;
    gpio_config_t gpio = {
        .pin_bit_mask = (1ULL << PCB2_PIN_KEY1) | (1ULL << PCB2_PIN_KEY2) |
                        (1ULL << PCB2_PIN_KEY3) | (1ULL << PCB2_PIN_KEY4),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err = gpio_config(&gpio);
    if (err != ESP_OK) return err;
    uint64_t now = esp_timer_get_time() / 1000;
    for (size_t i = 0; i < PCB2_KEY_COUNT; ++i) {
        bool down = gpio_get_level(key_pins[i]) == 0;
        keys[i] = (pcb2_debounce_t){down, down, now};
    }
    rmt_copy_encoder_config_t enc_config = {};
    err = rmt_new_copy_encoder(&enc_config, &encoder);
    rmt_tx_channel_config_t rmt = {
        .gpio_num = PCB2_PIN_RGB,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 20000000,
        .mem_block_symbols = 256,
        .trans_queue_depth = 1,
        .flags.with_dma = true
    };
    if (err == ESP_OK) err = rmt_new_tx_channel(&rmt, &strip);
    if (err == ESP_OK) {
        err = rmt_enable(strip);
        strip_enabled = err == ESP_OK;
    }
    if (err == ESP_OK) {
        memset(colors, 0, sizeof(colors));
        brightness = 32;
        err = pcb2_led_show();
    }
    if (err != ESP_OK) {
        if (strip_enabled) { rmt_disable(strip); strip_enabled = false; }
        if (strip) { rmt_del_channel(strip); strip = NULL; }
        if (encoder) { rmt_del_encoder(encoder); encoder = NULL; }
        gpio_reset_pin(PCB2_PIN_RGB);
        for (size_t i = 0; i < PCB2_KEY_COUNT; ++i) gpio_reset_pin(key_pins[i]);
    }
    return err;
}

esp_err_t pcb2_board_deinit(void) {
    if (!strip) return ESP_ERR_INVALID_STATE;
    memset(colors, 0, sizeof(colors));
    esp_err_t err = pcb2_led_show();
    esp_err_t deleted = rmt_disable(strip);
    if (deleted == ESP_OK) {
        strip_enabled = false;
        deleted = rmt_del_channel(strip);
    }
    if (deleted != ESP_OK) return deleted;
    strip = NULL;
    deleted = rmt_del_encoder(encoder);
    encoder = NULL;
    gpio_reset_pin(PCB2_PIN_RGB);
    for (size_t i = 0; i < PCB2_KEY_COUNT; ++i) gpio_reset_pin(key_pins[i]);
    return deleted != ESP_OK ? deleted : err;
}

pcb2_keys_t pcb2_keys_poll(void) {
    pcb2_keys_t result = {0};
    if (!strip) return result;
    uint64_t now = esp_timer_get_time() / 1000;
    for (size_t i = 0; i < PCB2_KEY_COUNT; ++i) {
        int edge = pcb2_debounce_step(&keys[i], gpio_get_level(key_pins[i]) == 0, now);
        if (keys[i].stable) result.down |= 1U << i;
        if (edge > 0) result.pressed |= 1U << i;
        if (edge < 0) result.released |= 1U << i;
    }
    return result;
}

esp_err_t pcb2_led_set(size_t index, uint8_t red, uint8_t green, uint8_t blue) {
    if (!strip) return ESP_ERR_INVALID_STATE;
    if (index >= PCB2_LED_COUNT) return ESP_ERR_INVALID_ARG;
    colors[index][0] = red; colors[index][1] = green; colors[index][2] = blue;
    return ESP_OK;
}

esp_err_t pcb2_led_fill(uint8_t red, uint8_t green, uint8_t blue) {
    if (!strip) return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < PCB2_LED_COUNT; ++i) pcb2_led_set(i, red, green, blue);
    return ESP_OK;
}

esp_err_t pcb2_led_brightness(uint8_t value) {
    if (!strip) return ESP_ERR_INVALID_STATE;
    brightness = value;
    return ESP_OK;
}

esp_err_t pcb2_led_show(void) {
    if (!strip || !encoder || !strip_enabled) return ESP_ERR_INVALID_STATE;
    size_t position = 0;
    for (size_t i = 0; i < PCB2_LED_COUNT; ++i) {
        const uint8_t grb[] = {
            pcb2_scale_color(colors[i][1], brightness),
            pcb2_scale_color(colors[i][0], brightness),
            pcb2_scale_color(colors[i][2], brightness)
        };
        for (size_t component = 0; component < 3; ++component) {
            for (int bit = 7; bit >= 0; --bit) {
                bool one = grb[component] & (1U << bit);
                symbols[position++] = (rmt_symbol_word_t){
                    .level0 = 1, .duration0 = one ? ONE_HIGH : ZERO_HIGH,
                    .level1 = 0, .duration1 = one ? ONE_LOW : ZERO_LOW
                };
            }
        }
    }
    symbols[position] = (rmt_symbol_word_t){
        .level0 = 0, .duration0 = RESET_HALF, .level1 = 0, .duration1 = RESET_HALF
    };
    rmt_transmit_config_t config = {0};
    esp_err_t err = rmt_transmit(strip, encoder, symbols, sizeof(symbols), &config);
    if (err == ESP_OK) err = rmt_tx_wait_all_done(strip, 1000);
    return err;
}
