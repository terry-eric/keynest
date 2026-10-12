#include "pcb2_board.h"
#include "board_logic.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static i2s_chan_handle_t tx, rx;
static bool tx_enabled, rx_enabled;
static portMUX_TYPE mic_mux = portMUX_INITIALIZER_UNLOCKED;
#if CONFIG_PCB2_MIC_RIGHT_SLOT
static bool mic_right = true;
#else
static bool mic_right;
#endif
static pcb2_audio_diagnostics_t mic_diagnostics;

void pcb2_audio_set_mic_right(bool right) {
    portENTER_CRITICAL(&mic_mux);
    mic_right = right;
    portEXIT_CRITICAL(&mic_mux);
}
bool pcb2_audio_mic_right(void) {
    portENTER_CRITICAL(&mic_mux);
    bool right = mic_right;
    portEXIT_CRITICAL(&mic_mux);
    return right;
}
void pcb2_audio_get_diagnostics(pcb2_audio_diagnostics_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&mic_mux);
    *out = mic_diagnostics;
    portEXIT_CRITICAL(&mic_mux);
}

esp_err_t pcb2_audio_deinit(void) {
    esp_err_t first = ESP_OK, err;
    if (rx_enabled) {
        err = i2s_channel_disable(rx);
        if (err == ESP_OK) rx_enabled = false;
        else first = err;
    }
    if (tx_enabled) {
        err = i2s_channel_disable(tx);
        if (err == ESP_OK) tx_enabled = false;
        else if (first == ESP_OK) first = err;
    }
    if (rx && !rx_enabled) {
        err = i2s_del_channel(rx);
        if (err == ESP_OK) rx = NULL;
        else if (first == ESP_OK) first = err;
    }
    if (tx && !tx_enabled) {
        err = i2s_del_channel(tx);
        if (err == ESP_OK) tx = NULL;
        else if (first == ESP_OK) first = err;
    }
    if (!tx && !rx) {
        const gpio_num_t pins[] = {PCB2_PIN_I2S_BCLK, PCB2_PIN_I2S_WS, PCB2_PIN_I2S_DOUT};
        for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
            gpio_set_level(pins[i], 0);
            gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
        }
    }
    return first;
}

esp_err_t pcb2_audio_init(uint32_t sample_rate) {
    if (tx || rx) return ESP_ERR_INVALID_STATE;
    if (sample_rate != 16000 && sample_rate != 32000 &&
        sample_rate != 44100 && sample_rate != 48000) return ESP_ERR_INVALID_ARG;
    i2s_chan_config_t channels = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channels.dma_desc_num = 6;
    channels.dma_frame_num = PCB2_AUDIO_BLOCK_FRAMES;
    channels.auto_clear = true; // Silence when TX is not supplied; no stale replay.
    esp_err_t err = i2s_new_channel(&channels, &tx, &rx);
    if (err != ESP_OK) return err;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PCB2_PIN_I2S_BCLK,
            .ws = PCB2_PIN_I2S_WS,
            .dout = PCB2_PIN_I2S_DOUT,
            .din = PCB2_PIN_I2S_DIN
        }
    };
    err = i2s_channel_init_std_mode(tx, &config);
    if (err == ESP_OK) err = i2s_channel_init_std_mode(rx, &config);
    if (err == ESP_OK) {
        err = i2s_channel_enable(tx);
        tx_enabled = err == ESP_OK;
    }
    if (err == ESP_OK) {
        err = i2s_channel_enable(rx);
        rx_enabled = err == ESP_OK;
    }
    if (err != ESP_OK) pcb2_audio_deinit();
    else vTaskDelay(pdMS_TO_TICKS(50)); // Covers ZTS6672's specified power-up time.
    return err;
}

esp_err_t pcb2_audio_read(int32_t *samples, size_t capacity, size_t *count, uint32_t timeout_ms) {
    if (!count) return ESP_ERR_INVALID_ARG;
    *count = 0;
    if (!samples || !capacity || capacity > PCB2_AUDIO_BLOCK_FRAMES) return ESP_ERR_INVALID_ARG;
    if (!rx_enabled) return ESP_ERR_INVALID_STATE;
    int32_t stereo[PCB2_AUDIO_BLOCK_FRAMES * 2];
    size_t bytes = 0;
    esp_err_t err = i2s_channel_read(rx, stereo, capacity * 8, &bytes, timeout_ms);
    *count = bytes / 8;
    const bool right = pcb2_audio_mic_right();
    pcb2_audio_diagnostics_t diagnostics = {.frames = *count};
    for (unsigned ch = 0; ch < 2; ++ch) {
        diagnostics.min[ch] = INT32_MAX;
        diagnostics.max[ch] = INT32_MIN;
    }
    for (size_t i = 0; i < *count; ++i) {
        samples[i] = pcb2_stereo_to_pcm24(&stereo[i * 2], right);
        for (unsigned ch = 0; ch < 2; ++ch) {
            int32_t value = pcb2_slot_to_pcm24(stereo[i * 2 + ch]);
            diagnostics.sum[ch] += value;
            diagnostics.squares[ch] += (int64_t)value * value;
            if (value < diagnostics.min[ch]) diagnostics.min[ch] = value;
            if (value > diagnostics.max[ch]) diagnostics.max[ch] = value;
        }
    }
    if (*count) {
        portENTER_CRITICAL(&mic_mux);
        mic_diagnostics = diagnostics;
        portEXIT_CRITICAL(&mic_mux);
    }
    if (bytes % 8) return ESP_FAIL;
    return err;
}

esp_err_t pcb2_audio_write(const int16_t *samples, size_t frames, size_t *written, uint32_t timeout_ms) {
    if (!written) return ESP_ERR_INVALID_ARG;
    *written = 0;
    if (!samples || !frames || frames > PCB2_AUDIO_BLOCK_FRAMES) return ESP_ERR_INVALID_ARG;
    if (!tx_enabled) return ESP_ERR_INVALID_STATE;
    int32_t stereo[PCB2_AUDIO_BLOCK_FRAMES * 2];
    for (size_t i = 0; i < frames; ++i) {
        stereo[i * 2] = stereo[i * 2 + 1] = pcb2_pcm16_to_slot(samples[i]);
    }
    size_t bytes = 0;
    esp_err_t err = i2s_channel_write(tx, stereo, frames * 8, &bytes, timeout_ms);
    *written = bytes / 8;
    if (bytes % 8) return ESP_FAIL;
    return err;
}
