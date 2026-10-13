#include "app_logic.h"
#include <string.h>
#include <math.h>

void logic_defaults(keypad_config_t *c) {
    memset(c, 0, sizeof(*c));
    c->schema = 1; c->led_mode = LED_RIPPLE; c->brightness = 64;
    c->speed = 80; c->volume = 10; c->mic_gain = 2; c->sound = SOUND_CHIME;
    c->color[0] = 120; c->color[1] = 88; c->color[2] = 255;
    for (unsigned i = 0; i < 4; ++i) c->keycodes[i] = 0x68 + i; // F13..F16
}
bool logic_config_valid(const keypad_config_t *c) {
    if (!c || c->schema != 1 || c->led_mode >= LED_MODE_COUNT || c->sound >= SOUND_COUNT ||
        c->speed < 20 || c->speed > 200 || c->volume > 100 || c->mic_gain < 1 || c->mic_gain > 16) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (c->keycodes[i] && (c->keycodes[i] < 4 || c->keycodes[i] > 0x73)) return false;
    return true;
}
void logic_hid_report(uint8_t down, const keypad_config_t *c, keypad_report_t *r) {
    memset(r, 0, sizeof(*r));
    unsigned count = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (!(down & (1U << i))) continue;
        r->modifiers |= c->modifiers[i];
        uint8_t code = c->keycodes[i];
        bool duplicate = false;
        for (unsigned j = 0; j < count; ++j) duplicate |= r->keys[j] == code;
        if (code && !duplicate) r->keys[count++] = code;
    }
}
int16_t logic_mic_pcm16(int32_t pcm24, unsigned gain) {
    int64_t value = (int64_t)pcm24 * gain / 256;
    if (value > 32767) value = 32767;
    if (value < -32768) value = -32768;
    return (int16_t)value;
}
static void le16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void le32(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = n >> (8 * i); }
void logic_wav_header(uint8_t h[44], uint32_t frames) {
    memset(h, 0, 44);
    memcpy(h, "RIFF", 4); le32(h + 4, 36 + frames * 2); memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16); le16(h + 20, 1); le16(h + 22, 1);
    le32(h + 24, CAPTURE_RATE); le32(h + 28, CAPTURE_RATE * 2);
    le16(h + 32, 2); le16(h + 34, 16); memcpy(h + 36, "data", 4); le32(h + 40, frames * 2);
}
uint32_t logic_crc32(uint32_t seed, const void *data, size_t length) {
    const uint8_t *bytes = data; uint32_t crc = ~seed;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    return ~crc;
}
void logic_voice_start(keypad_voice_t *v, unsigned key, unsigned effect) {
    const unsigned lengths[] = {0, 3360, 12480, 7200, 8640, 0};
    *v = (keypad_voice_t){.effect = effect < SOUND_COUNT ? effect : 0, .key = key % 4};
    v->length = lengths[v->effect];
}
int16_t logic_sound_sample(keypad_voice_t voices[4], unsigned volume) {
    static const float notes[] = {261.626f, 329.628f, 391.995f, 523.251f};
    float sum = 0;
    for (unsigned i = 0; i < 4; ++i) {
        keypad_voice_t *v = &voices[i];
        if (v->position >= v->length) continue;
        float t = v->position / (float)KEYPAD_RATE, freq = notes[v->key], wave;
        float envelope = fminf(1, fminf(v->position / 240.0f, (v->length - v->position) / 960.0f));
        if (v->effect == SOUND_CLICK) wave = sinf(6.2831853f * (freq * 4) * t) * expf(-50 * t);
        else if (v->effect == SOUND_CHIME)
            wave = (sinf(6.2831853f * freq * t) + 0.35f * sinf(6.2831853f * freq * 1.5f * t)) * expf(-9 * t) * 0.7f;
        else if (v->effect == SOUND_RETRO) {
            float phase = 6.2831853f * freq * (t < 0.075f ? 2 : 1) * t;
            wave = sinf(phase) > 0 ? 0.45f : -0.45f;
        } else wave = sinf(6.2831853f * (freq * t + 1400 * t * t)) * expf(-12 * t);
        sum += wave * envelope; ++v->position;
    }
    sum = fmaxf(-1, fminf(1, sum));
    unsigned limited_volume = volume > 100 ? 100 : volume;
    return (int16_t)(sum * (16000.0f * limited_volume / 100.0f));
}
static void hue(float h, uint8_t color[3]) {
    float x = 1 - fabsf(fmodf(h / 60, 2) - 1), r = 0, g = 0, b = 0;
    if (h < 60) { r = 1; g = x; } else if (h < 120) { r = x; g = 1; }
    else if (h < 180) { g = 1; b = x; } else if (h < 240) { g = x; b = 1; }
    else if (h < 300) { r = x; b = 1; } else { r = 1; b = x; }
    color[0] = r * 255; color[1] = g * 255; color[2] = b * 255;
}
void logic_led_frame(const keypad_config_t *c, uint64_t now, const uint64_t pulses[4],
                     uint8_t down, float mic_peak, uint8_t rgb[4][3]) {
    float phase = (float)fmod((double)now * c->speed / 250000.0, 1.0);
    for (unsigned i = 0; i < 4; ++i) {
        float level = 1;
        uint8_t color[3]; memcpy(color, c->color, 3);
        switch (c->led_mode) {
            case LED_OFF: level = 0; break;
            case LED_BREATHE: level = 0.08f + 0.92f * (1 - cosf(phase * 6.2831853f)) / 2; break;
            case LED_RAINBOW: hue(fmodf(phase * 360 + i * 75, 360), color); break;
            case LED_RIPPLE:
                level = 0.06f;
                for (unsigned j = 0; j < 4; ++j) {
                    if (!pulses[j] || now < pulses[j]) continue;
                    float age = (now - pulses[j]) * c->speed / 100.0f;
                    int distance = (int)i - (int)j; if (distance < 0) distance = -distance;
                    float delayed = age - distance * 75;
                    if (delayed >= 0 && delayed < 450) level = fmaxf(level, 1 - delayed / 450);
                }
                break;
            case LED_METER:
                level = fmaxf(0.03f, fminf(1, mic_peak * 4 - i));
                color[0] = i >= 3 ? 255 : 32; color[1] = i >= 3 ? 64 : 255; color[2] = 120;
                break;
            default: break;
        }
        if (c->led_mode != LED_OFF && (down & (1U << i))) level = 1;
        for (unsigned j = 0; j < 3; ++j) rgb[i][j] = (uint8_t)(color[j] * level);
    }
}
