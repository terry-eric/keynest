#include "../firmware/components/pcb2_board/board_logic.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>

int main(void) {
    pcb2_debounce_t key = {false, false, 0};
    assert(pcb2_debounce_step(&key, true, 1) == 0);
    assert(pcb2_debounce_step(&key, false, 6) == 0);
    assert(pcb2_debounce_step(&key, true, 9) == 0);
    assert(pcb2_debounce_step(&key, true, 28) == 0);
    assert(pcb2_debounce_step(&key, true, 29) == 1);
    assert(pcb2_debounce_step(&key, true, 100) == 0);
    assert(pcb2_debounce_step(&key, false, 101) == 0);
    assert(pcb2_debounce_step(&key, false, 121) == -1);
    assert(pcb2_debounce_step(&key, false, 122) == 0);
    pcb2_debounce_t held = {true, true, 0};
    assert(pcb2_debounce_step(&held, true, 20) == 0);
    pcb2_debounce_t long_uptime = {false, false, 0};
    assert(pcb2_debounce_step(&long_uptime, true, 4294967290ULL) == 0);
    assert(pcb2_debounce_step(&long_uptime, true, 4294967310ULL) == 1);
    for (int pcm = INT16_MIN; pcm <= INT16_MAX; ++pcm) {
        assert(pcb2_pcm16_to_slot((int16_t)pcm) == (int64_t)pcm * 65536);
        assert(pcb2_slot_to_pcm24(pcb2_pcm16_to_slot((int16_t)pcm)) == pcm * 256);
    }
    assert(pcb2_slot_to_pcm24(INT32_MIN) == -8388608);
    assert(pcb2_slot_to_pcm24(INT32_MAX) == 8388607);
    assert(pcb2_slot_to_pcm24(-1) == -1);
    for (int color = 0; color <= 255; ++color) {
        assert(pcb2_scale_color(color, 0) == 0);
        assert(pcb2_scale_color(color, 255) == color);
        for (int brightness = 0; brightness <= 255; ++brightness)
            assert(pcb2_scale_color(color, brightness) <= color);
    }
    puts("PASS: debounce bounce/held/long-uptime, 65536 signed PCM values, mic endpoints, 65536 brightness combinations");
    return 0;
}
