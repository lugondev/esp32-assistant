#pragma once
#include <stdbool.h>

// Pure, host-testable hold/release timing for a single button — no GPIO or
// RTOS access. The button driver's 20ms debounce scan tick is the unit of
// time here; a caller polling at a different rate must scale the thresholds.
#define BTN_HOLD_TICK_MS 20
#define BTN_HOLD_SETUP_TICKS   (10000 / BTN_HOLD_TICK_MS)  // 10s -> setup portal
#define BTN_HOLD_ERASE_TICKS   (15000 / BTN_HOLD_TICK_MS)  // 15s -> erase all data
#define BTN_HOLD_CONFIRM_TICKS  (3000 / BTN_HOLD_TICK_MS)  //  3s -> confirm the erase

typedef enum {
    BTN_HOLD_EVENT_NONE,             // nothing to report this tick
    BTN_HOLD_EVENT_RELEASE,          // released before the 10s setup threshold
    BTN_HOLD_EVENT_WARN_SETUP,       // just crossed 10s, still held (warn only)
    BTN_HOLD_EVENT_WARN_ERASE,       // just crossed 15s, still held (warn only)
    BTN_HOLD_EVENT_SETUP,            // released in [10s, 15s) -> setup portal
    BTN_HOLD_EVENT_ERASE_ARM,        // released at >= 15s -> ask to confirm
    BTN_HOLD_EVENT_ERASE_CONFIRMED,  // confirm-mode hold reached 3s -> erase
    BTN_HOLD_EVENT_CONFIRM_ABORT,    // confirm-mode press let go before 3s
} btn_hold_event_t;

// Caller-owned per-button state. Zero-initialised is a valid "no press in
// progress"; btn_hold_step resets it itself on every release.
typedef struct {
    int  ticks;         // ticks the current press has been held
    bool warned_setup;  // WARN_SETUP already fired for this press
    bool warned_erase;  // WARN_ERASE already fired for this press
    bool fired;         // confirm-mode: 3s threshold already reached this press
} btn_hold_state_t;

void btn_hold_reset(btn_hold_state_t *st);

// Call once per scan tick while the button is confirmed pressed, plus once on
// the tick it reads released.
//
// The 10s action deliberately lands on RELEASE, not on the threshold crossing:
// acting at 10s while the button is still down would reboot into the portal
// before the user could ever reach 15s. The threshold crossings only warn, so
// the display can tell the user what letting go now will do.
//
// released:     true if the pin reads "not pressed" on this tick.
// confirm_mode: true while the caller is waiting for the erase to be confirmed.
//               The whole ladder collapses to a single 3s threshold: reaching it
//               fires ERASE_CONFIRMED, letting go early fires CONFIRM_ABORT.
//               A confirm-mode press never fires the setup/erase warnings.
btn_hold_event_t btn_hold_step(bool released, bool confirm_mode, btn_hold_state_t *st);
