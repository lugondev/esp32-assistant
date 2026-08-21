#include "button_hold_logic.h"

void btn_hold_reset(btn_hold_state_t *st) {
    st->ticks = 0;
    st->warned_setup = false;
    st->warned_erase = false;
    st->fired = false;
}

btn_hold_event_t btn_hold_step(bool released, bool confirm_mode, btn_hold_state_t *st) {
    if (confirm_mode) {
        // One threshold, nothing else. Letting go early is the abort path, so
        // the only way to erase is a deliberate second hold — a stray tap on
        // the confirmation screen cancels rather than confirms.
        if (released) {
            bool confirmed = st->fired;
            btn_hold_reset(st);
            return confirmed ? BTN_HOLD_EVENT_NONE : BTN_HOLD_EVENT_CONFIRM_ABORT;
        }
        if (st->fired) return BTN_HOLD_EVENT_NONE;
        if (++st->ticks >= BTN_HOLD_CONFIRM_TICKS) {
            st->fired = true;
            return BTN_HOLD_EVENT_ERASE_CONFIRMED;
        }
        return BTN_HOLD_EVENT_NONE;
    }

    // Normal mode. The action is chosen by how long the press lasted, and is
    // reported on the release tick — see the header for why acting on the
    // threshold crossing itself would make 15s unreachable.
    if (released) {
        int held = st->ticks;
        btn_hold_reset(st);
        if (held >= BTN_HOLD_ERASE_TICKS) return BTN_HOLD_EVENT_ERASE_ARM;
        if (held >= BTN_HOLD_SETUP_TICKS) return BTN_HOLD_EVENT_SETUP;
        return BTN_HOLD_EVENT_RELEASE;
    }

    st->ticks++;
    // Ordered erase-first so the 15s crossing is not masked by the 10s one on
    // the same tick — they can only coincide if the thresholds are equal, but
    // the ladder should degrade sanely if someone retunes them.
    if (!st->warned_erase && st->ticks >= BTN_HOLD_ERASE_TICKS) {
        st->warned_erase = true;
        return BTN_HOLD_EVENT_WARN_ERASE;
    }
    if (!st->warned_setup && st->ticks >= BTN_HOLD_SETUP_TICKS) {
        st->warned_setup = true;
        return BTN_HOLD_EVENT_WARN_SETUP;
    }
    return BTN_HOLD_EVENT_NONE;
}
