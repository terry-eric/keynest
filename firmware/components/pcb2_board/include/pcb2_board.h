#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "pcb2_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

// All lifecycle operations must run with peripheral workers stopped. One board
// instance is supported. Keys/LEDs, audio, and SD have independent lifecycles.
// A single task owns each subsystem; calls are not internally serialized.
typedef struct {
    uint8_t down;      // Bit 0 = KEY1 ... bit 3 = KEY4; active low hardware.
    uint8_t pressed;   // Edges since the previous poll, not a persistent queue.
    uint8_t released;
} pcb2_keys_t;

esp_err_t pcb2_board_init(void);
esp_err_t pcb2_board_deinit(void);
// Call every 1-10 ms; returns an all-zero snapshot before initialization.
pcb2_keys_t pcb2_keys_poll(void);
// Logical RGB values 0..255; global brightness starts at 32/255.
esp_err_t pcb2_led_set(size_t index, uint8_t red, uint8_t green, uint8_t blue);
esp_err_t pcb2_led_fill(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t pcb2_led_brightness(uint8_t brightness);
esp_err_t pcb2_led_show(void);

#define PCB2_AUDIO_BLOCK_FRAMES 128
#define PCB2_AUDIO_DEFAULT_RATE 48000
// Shared I2S0: Philips I2S, 32-bit stereo slots, 64 BCLK/frame. The microphone
// slot follows CONFIG_PCB2_MIC_RIGHT_SLOT. TX repeats mono for NS4168.
// Supported sample rates: 16000, 32000, 44100, 48000 Hz.
esp_err_t pcb2_audio_init(uint32_t sample_rate);
esp_err_t pcb2_audio_deinit(void);
// Thread-safe diagnostic slot selection; caller must stop recording first.
// Does not alter I2S clocks, format, gain, or persistent configuration.
void pcb2_audio_set_mic_right(bool right);
bool pcb2_audio_mic_right(void);
typedef struct {
    size_t frames;
    int32_t min[2], max[2];
    int64_t sum[2];
    uint64_t squares[2];
} pcb2_audio_diagnostics_t;
// Latest DMA block, both slots, signed PCM24 before digital gain. Copy is locked.
void pcb2_audio_get_diagnostics(pcb2_audio_diagnostics_t *out);
// Signed 24-bit microphone samples, right-aligned in int32_t. capacity: 1..128.
// On timeout, count can be nonzero; process the returned frames before retrying.
esp_err_t pcb2_audio_read(int32_t *samples, size_t capacity, size_t *count, uint32_t timeout_ms);
// Signed 16-bit mono speaker PCM; frames: 1..128. No hidden volume boost.
// On timeout, written can be nonzero; retry only the remaining frames.
esp_err_t pcb2_audio_write(const int16_t *samples, size_t frames, size_t *written, uint32_t timeout_ms);

// Mount at /sdcard using SPI2, 10 MHz. Never formats an unmountable card.
// Card-detect contacts are not wired to GPIO: failures are reported by mount/IO.
esp_err_t pcb2_sd_mount(void);
esp_err_t pcb2_sd_unmount(void);
bool pcb2_sd_is_mounted(void);

#ifdef __cplusplus
}
#endif
