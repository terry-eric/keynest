#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define KEYPAD_RATE 48000U
#define CAPTURE_RATE 16000U
#define FLASH_CAPTURE_FRAMES 32768U
#define KEYPAD_KEYS 4
enum { LED_OFF, LED_SOLID, LED_BREATHE, LED_RAINBOW, LED_RIPPLE, LED_METER, LED_MODE_COUNT };
enum { SOUND_OFF, SOUND_CLICK, SOUND_CHIME, SOUND_RETRO, SOUND_BUBBLE, SOUND_CUSTOM, SOUND_COUNT };
typedef struct {
    uint32_t schema;
    uint8_t led_mode, brightness, speed, volume, mic_gain, sound;
    uint8_t color[3], keycodes[4], modifiers[4];
} keypad_config_t;
typedef struct { uint8_t modifiers, reserved, keys[6]; } keypad_report_t;
typedef struct { unsigned position, length; uint8_t effect, key; } keypad_voice_t;
typedef struct { uint32_t gain; } keypad_limiter_t;
void logic_defaults(keypad_config_t *config);
bool logic_config_valid(const keypad_config_t *config);
void logic_hid_report(uint8_t down, const keypad_config_t *config, keypad_report_t *report);
int16_t logic_mic_pcm16(int32_t pcm24, unsigned gain);
void logic_wav_header(uint8_t header[44], uint32_t frames);
uint32_t logic_crc32(uint32_t seed, const void *data, size_t length);
void logic_voice_start(keypad_voice_t *voice, unsigned key, unsigned effect);
int32_t logic_sound_sample(keypad_voice_t voices[4], unsigned volume);
int16_t logic_limit_sample(keypad_limiter_t *limiter, int32_t sample);
void logic_led_frame(const keypad_config_t *config, uint64_t now_ms,
                     const uint64_t pulses[4], uint8_t down, float mic_peak, uint8_t rgb[4][3]);
