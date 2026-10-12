#include "unity.h"
#include "board_logic.h"
#include "pcb2_board.h"

// Referenced by the test app so archive linkers retain this translation unit
// and its Unity constructor registrations, including with PlatformIO/SCons.
void pcb2_board_tests_link(void) {}

TEST_CASE("Contact bounce does not cause repeated press or release", "[pcb2]") {
    pcb2_debounce_t key = {false, false, 0};
    TEST_ASSERT_EQUAL(0, pcb2_debounce_step(&key, true, 1));
    TEST_ASSERT_EQUAL(0, pcb2_debounce_step(&key, false, 6));
    TEST_ASSERT_EQUAL(0, pcb2_debounce_step(&key, true, 9));
    TEST_ASSERT_EQUAL(0, pcb2_debounce_step(&key, true, 28));
    TEST_ASSERT_EQUAL(1, pcb2_debounce_step(&key, true, 29));
    TEST_ASSERT_EQUAL(0, pcb2_debounce_step(&key, true, 100));
    TEST_ASSERT_EQUAL(0, pcb2_debounce_step(&key, false, 101));
    TEST_ASSERT_EQUAL(-1, pcb2_debounce_step(&key, false, 121));
    TEST_ASSERT_EQUAL(0, pcb2_debounce_step(&key, false, 122));
}

TEST_CASE("Signed PCM preserves endpoints and ignores microphone unused bits", "[pcb2]") {
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, pcb2_pcm16_to_slot(INT16_MIN));
    TEST_ASSERT_EQUAL_INT32(0x7fff0000, pcb2_pcm16_to_slot(INT16_MAX));
    TEST_ASSERT_EQUAL_INT32(-65536, pcb2_pcm16_to_slot(-1));
    TEST_ASSERT_EQUAL_INT32(-8388608, pcb2_slot_to_pcm24(INT32_MIN));
    TEST_ASSERT_EQUAL_INT32(8388607, pcb2_slot_to_pcm24(INT32_MAX));
    TEST_ASSERT_EQUAL_INT32(-1, pcb2_slot_to_pcm24(-1));
}

TEST_CASE("Brightness keeps colors recoverable and boundaries exact", "[pcb2]") {
    TEST_ASSERT_EQUAL_UINT8(0, pcb2_scale_color(255, 0));
    TEST_ASSERT_EQUAL_UINT8(255, pcb2_scale_color(255, 255));
    TEST_ASSERT_EQUAL_UINT8(32, pcb2_scale_color(255, 32));
    TEST_ASSERT_EQUAL_UINT8(128, pcb2_scale_color(128, 255));
}

TEST_CASE("Uninitialized peripherals and invalid buffers fail clearly", "[pcb2]") {
    int32_t input[1]; int16_t output[1]; size_t count = 99;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pcb2_audio_read(NULL, 1, &count, 0));
    TEST_ASSERT_EQUAL_UINT32(0, count);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pcb2_audio_read(input, 1, &count, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pcb2_audio_write(output, 1, &count, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pcb2_audio_read(input, 129, &count, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pcb2_led_set(0, 0, 0, 0));
    TEST_ASSERT_FALSE(pcb2_sd_is_mounted());
    TEST_ASSERT_EQUAL_UINT8(0, pcb2_keys_poll().down);
}
