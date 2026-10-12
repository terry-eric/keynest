#include <assert.h>
#include <stdio.h>
#include "../firmware/keyboard/main/mic_monitor.h"

static int64_t now = 1000;
int64_t esp_timer_get_time(void) { return now; }

int main(void) {
    int16_t output[MIC_MONITOR_FRAMES], input[2000];
    mic_monitor_snapshot_t snapshot;
    mic_monitor_read(output, MIC_MONITOR_FRAMES, &snapshot);
    assert(snapshot.count == 0 && snapshot.frames == 0);
    for (unsigned i = 0; i < 2000; ++i) input[i] = (int16_t)i - 1000;
    mic_monitor_feed(input, 32);
    mic_monitor_read(output, 16, &snapshot);
    assert(snapshot.count == 16 && snapshot.frames == 32 && snapshot.captured_us == now);
    for (unsigned i = 0; i < 16; ++i) assert(output[i] == input[i + 16]);
    now = 2000;
    mic_monitor_feed(input + 32, 1968);
    mic_monitor_read(output, MIC_MONITOR_FRAMES, &snapshot);
    assert(snapshot.count == MIC_MONITOR_FRAMES && snapshot.frames == 2000 && snapshot.captured_us == now);
    for (unsigned i = 0; i < MIC_MONITOR_FRAMES; ++i) assert(output[i] == input[2000 - MIC_MONITOR_FRAMES + i]);
    mic_monitor_read(output, 3, &snapshot);
    for (unsigned i = 0; i < 3; ++i) assert(output[i] == input[1997 + i]);
    mic_monitor_read(output, MIC_MONITOR_FRAMES, &snapshot);
    assert(snapshot.count == MIC_MONITOR_FRAMES); // Multiple readers do not consume samples.
    mic_monitor_feed(NULL, 0);
    mic_monitor_read(output, 0, &snapshot);
    assert(snapshot.count == 0 && snapshot.frames == 2000);
    puts("PASS: signed PCM preview order, wrap, timestamps, bounded snapshots and independent readers");
    return 0;
}
