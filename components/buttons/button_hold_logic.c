#include "button_hold_logic.h"

btn_hold_event_t btn_hold_step(bool released, int *held_ticks, bool *hold_fired) {
    if (released) {
        return (*hold_fired) ? BTN_HOLD_EVENT_NONE : BTN_HOLD_EVENT_RELEASE;
    }
    if (*hold_fired) return BTN_HOLD_EVENT_NONE;  // already fired; wait for release
    (*held_ticks)++;
    if (*held_ticks >= BTN_HOLD_THRESHOLD_TICKS) {
        *hold_fired = true;
        return BTN_HOLD_EVENT_HOLD;
    }
    return BTN_HOLD_EVENT_NONE;
}
