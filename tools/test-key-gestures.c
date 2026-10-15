#include "key_gestures.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 key_gestures_t s;uint8_t e[4];uint16_t h[4]={600,0,600,0},d[4]={300,300,0,0};
 key_gestures_reset(&s,0);
 key_gestures_tick(&s,15,0,h,d,e);assert(!e[0]&&!e[1]&&!e[2]&&e[3]==KEY_GESTURE_TAP);
 key_gestures_tick(&s,0,100,h,d,e);assert(!e[0]&&!e[1]&&e[2]==KEY_GESTURE_TAP&&!e[3]);
 key_gestures_tick(&s,0,400,h,d,e);assert(!e[0]&&!e[1]);
 key_gestures_tick(&s,0,401,h,d,e);assert(e[0]==KEY_GESTURE_TAP&&e[1]==KEY_GESTURE_TAP);
 // A second down exactly at the interval boundary belongs to the double click.
 key_gestures_tick(&s,3,500,h,d,e);key_gestures_tick(&s,0,550,h,d,e);
 key_gestures_tick(&s,3,850,h,d,e);assert(!e[0]&&!e[1]);
 key_gestures_tick(&s,0,900,h,d,e);assert(e[0]==KEY_GESTURE_DOUBLE&&e[1]==KEY_GESTURE_DOUBLE);
 key_gestures_tick(&s,5,1000,h,d,e);key_gestures_tick(&s,5,1599,h,d,e);assert(!e[0]&&!e[2]);
 key_gestures_tick(&s,5,1600,h,d,e);assert(e[0]==KEY_GESTURE_HOLD&&e[2]==KEY_GESTURE_HOLD);
 key_gestures_tick(&s,5,99999,h,d,e);assert(!e[0]&&!e[2]);key_gestures_tick(&s,0,100000,h,d,e);assert(!e[0]&&!e[2]);
 // Held keys across reconnect/configuration need a fresh press; no stale release tap.
 key_gestures_reset(&s,7);key_gestures_tick(&s,7,200000,h,d,e);assert(!e[0]&&!e[1]&&!e[2]);
 key_gestures_tick(&s,0,200100,h,d,e);assert(!e[0]&&!e[1]&&!e[2]);key_gestures_tick(&s,0,200999,h,d,e);assert(!e[0]&&!e[1]);
 puts("PASS: independent four-key gesture timing, exact thresholds, hold-only/double-only, exclusive actions, no repeat or reconnect replay");return 0;
}
