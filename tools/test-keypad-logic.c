#include "../firmware/keyboard/main/app_logic.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#undef assert
#define assert(condition) do { if (!(condition)) { fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
int main(void) {
    keypad_config_t c; logic_defaults(&c); assert(logic_config_valid(&c));
    keypad_report_t report;
    for (unsigned mask = 0; mask < 16; ++mask) {
        logic_hid_report(mask, &c, &report); unsigned count = 0;
        for (unsigned i = 0; i < 4; ++i) if (mask & (1U << i)) assert(report.keys[count++] == 104 + i);
        for (unsigned i = count; i < 6; ++i) assert(report.keys[i] == 0);
    }
    c.keycodes[0] = c.keycodes[1] = 4; c.modifiers[0] = 1; c.modifiers[1] = 2;
    logic_hid_report(3, &c, &report); assert(report.modifiers == 3 && report.keys[0] == 4 && report.keys[1] == 0);
    logic_hid_report(0, &c, &report); assert(report.modifiers == 0 && report.keys[0] == 0);
    c.keycodes[0] = 0; logic_hid_report(1, &c, &report); assert(report.modifiers == 1 && report.keys[0] == 0);
    c.keycodes[0] = 3; assert(!logic_config_valid(&c)); c.keycodes[0] = 116; assert(!logic_config_valid(&c));
    logic_defaults(&c); c.speed = 19; assert(!logic_config_valid(&c)); c.speed = 200; assert(logic_config_valid(&c));
    assert(logic_mic_pcm16(-8388608, 1) == -32768 && logic_mic_pcm16(8388607, 1) == 32767);
    assert(logic_mic_pcm16(8388607, 16) == 32767 && logic_mic_pcm16(-8388608, 16) == -32768);
    uint8_t header[44]; logic_wav_header(header, 32768);
    assert(!memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVEfmt ", 8));
    assert(le32(header + 4) == 65572 && le32(header + 24) == 16000 && le32(header + 28) == 32000 && le32(header + 40) == 65536);
    assert(logic_crc32(0, "123456789", 9) == 0xcbf43926U);
    assert(logic_crc32(logic_crc32(0, "1234", 4), "56789", 5) == 0xcbf43926U);
    for (unsigned effect = 0; effect < SOUND_COUNT; ++effect) {
        keypad_voice_t voices[4]; for (unsigned i = 0; i < 4; ++i) logic_voice_start(&voices[i], i, effect);
        int initial = logic_sound_sample(voices, 100);
        if (initial) fprintf(stderr, "Effect %u starts at %d\n", effect, initial);
        assert(initial == 0);
        bool audible = false;
        keypad_limiter_t limiter={65536};
        for (unsigned i = 0; i < 15000; ++i) { int sample = logic_limit_sample(&limiter,logic_sound_sample(voices, 100)); assert(sample >= -28000 && sample <= 28000); audible |= sample != 0; }
        assert(audible == (effect != SOUND_OFF && effect != SOUND_CUSTOM)); assert(logic_sound_sample(voices, 100) == 0);
        logic_voice_start(&voices[0], 0, effect); for (unsigned i = 0; i < 1000; ++i) assert(logic_sound_sample(voices, 0) == 0);
    }
    keypad_limiter_t limiter={65536};
    assert(logic_limit_sample(&limiter,12000)==12000); // Quiet signals retain their amplitude.
    assert(logic_limit_sample(&limiter,120000)>27990); // Four full-scale voices stay bounded.
    assert(limiter.gain<65536);
    uint32_t reduced=limiter.gain;
    for(unsigned i=0;i<1000;++i) {
        int sample=logic_limit_sample(&limiter,i%2?-131072:131068);
        assert(sample>=-28000 && sample<=28000);
    }
    uint32_t before=limiter.gain;logic_limit_sample(&limiter,12000);
    assert(limiter.gain>before && limiter.gain-before<30); // No abrupt gain jump when voices end.
    for(unsigned i=0;i<48000;++i)logic_limit_sample(&limiter,0);
    assert(limiter.gain==65536);assert(logic_limit_sample(&limiter,12000)==12000);
    (void)reduced;
    uint8_t rgb[4][3]; uint64_t pulses[4] = {100,0,0,0}; logic_defaults(&c);
    c.led_mode = LED_OFF; logic_led_frame(&c, 150, pulses, 15, 1, rgb); for (unsigned i = 0; i < sizeof(rgb); ++i) assert(((uint8_t *)rgb)[i] == 0);
    c.led_mode = LED_SOLID; logic_led_frame(&c, 150, pulses, 0, 0, rgb); for (unsigned i = 0; i < 4; ++i) assert(!memcmp(rgb[i], c.color, 3));
    c.led_mode = LED_RIPPLE; logic_led_frame(&c, 150, pulses, 8, 0, rgb); assert(!memcmp(rgb[3], c.color, 3));
    puts("PASS: HID chords/duplicate usages/release, config ranges, mic saturation, 16kHz WAV, CRC, SFX bounds/endpoints/mute, LED modes");
    return 0;
}
