#pragma once
#include <stdbool.h>

// Pure, host-testable hold/release timing for a single button — no GPIO or
// RTOS access. The button driver's 20ms debounce scan tick is the unit of
// time here; a caller polling at a different rate must scale the threshold.
#define BTN_HOLD_TICK_MS 20
#define BTN_HOLD_THRESHOLD_TICKS (10000 / BTN_HOLD_TICK_MS)  // 10s

typedef enum {
    BTN_HOLD_EVENT_NONE,     // nothing to report this tick
    BTN_HOLD_EVENT_RELEASE,  // released before reaching the hold threshold
    BTN_HOLD_EVENT_HOLD,     // just crossed the threshold, still held
} btn_hold_event_t;

// Call once per scan tick while the button is confirmed held (i.e. every
// tick between debounce-confirm and the release that ends the press).
// `held_ticks`/`hold_fired` are the caller-owned persistent state for THIS
// button, reset to (0, false) by the caller when a new press begins.
//
// released: true if the pin reads "not pressed" on this tick.
//
// Fires BTN_HOLD_EVENT_HOLD exactly once per press, the first tick the
// threshold is reached. After that, further ticks (still held) return
// NONE, and the eventual release also returns NONE — a press that became
// a hold never additionally reports a release.
btn_hold_event_t btn_hold_step(bool released, int *held_ticks, bool *hold_fired);
