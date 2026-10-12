#include "pcb2_board.h"
#include "board_logic.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "led_strip.h"
#include <string.h>

static const gpio_num_t key_pins[PCB2_KEY_COUNT] = {
    PCB2_PIN_KEY1, PCB2_PIN_KEY2, PCB2_PIN_KEY3, PCB2_PIN_KEY4
};
static pcb2_debounce_t keys[PCB2_KEY_COUNT];
static uint8_t colors[PCB2_LED_COUNT][3];
static uint8_t brightness = 32;
static led_strip_handle_t strip;

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
    led_strip_config_t config = {
        .strip_gpio_num = PCB2_PIN_RGB,
        .max_leds = PCB2_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,
        .mem_block_symbols = 64,
        .flags.with_dma = false
    };
    err = led_strip_new_rmt_device(&config, &rmt, &strip);
    if (err == ESP_OK) {
        memset(colors, 0, sizeof(colors));
        brightness = 32;
        err = led_strip_clear(strip);
    }
    if (err != ESP_OK) {
        if (strip) { led_strip_del(strip); strip = NULL; }
        for (size_t i = 0; i < PCB2_KEY_COUNT; ++i) gpio_reset_pin(key_pins[i]);
    }
    return err;
}

esp_err_t pcb2_board_deinit(void) {
    if (!strip) return ESP_ERR_INVALID_STATE;
    esp_err_t err = led_strip_clear(strip);
    esp_err_t deleted = led_strip_del(strip);
    if (deleted != ESP_OK) return deleted;
    strip = NULL;
    for (size_t i = 0; i < PCB2_KEY_COUNT; ++i) gpio_reset_pin(key_pins[i]);
    return err;
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
    if (!strip) return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < PCB2_LED_COUNT; ++i) {
        esp_err_t err = led_strip_set_pixel(strip, i,
            pcb2_scale_color(colors[i][0], brightness),
            pcb2_scale_color(colors[i][1], brightness),
            pcb2_scale_color(colors[i][2], brightness));
        if (err != ESP_OK) return err;
    }
    return led_strip_refresh(strip);
}
