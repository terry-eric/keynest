#include "key_gestures.h"
#include <string.h>

void key_gestures_reset(key_gestures_t *s, uint8_t down) {
    memset(s, 0, sizeof(*s));
    // A key held across reconnect/configuration must first be released.
    s->previous = down; s->held = down;
}
void key_gestures_tick(key_gestures_t *s, uint8_t down, uint64_t now,
                       const uint16_t hold[4], const uint16_t twice[4], uint8_t events[4]) {
    memset(events, 0, 4);
    for (unsigned i=0;i<4;i++) {
        uint8_t bit=1U<<i;
        bool pressed=(down & bit) && !(s->previous & bit), released=!(down & bit) && (s->previous & bit);
        if (!hold[i] && !twice[i]) {
            if (pressed) events[i]=KEY_GESTURE_TAP;
            continue;
        }
        if ((s->pending & bit) && !(s->second & bit) && now-s->released_at[i]>twice[i]) {
            events[i]=KEY_GESTURE_TAP; s->pending &= ~bit;
        }
        if (pressed) {
            s->pressed_at[i]=now; s->held &= ~bit;
            if (s->pending & bit) s->second |= bit;
        }
        if ((down & bit) && !(s->held & bit) && hold[i] && now-s->pressed_at[i]>=hold[i]) {
            events[i]=KEY_GESTURE_HOLD; s->held |= bit; s->pending &= ~bit; s->second &= ~bit;
        }
        if (released) {
            if (s->held & bit) { s->held &= ~bit; continue; }
            if (s->second & bit) {
                events[i]=KEY_GESTURE_DOUBLE; s->second &= ~bit; s->pending &= ~bit;
            } else if (twice[i]) { s->pending |= bit; s->released_at[i]=now; }
            else events[i]=KEY_GESTURE_TAP;
        }
    }
    s->previous=down;
}
