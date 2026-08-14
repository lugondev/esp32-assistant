#include "loopback_logic.h"

void lb_init(lb_t *lb, int capacity) {
    lb->state = LB_IDLE;
    lb->capacity = capacity;
    lb->filled = 0;
}

lb_action_t lb_on_press(lb_t *lb) {
    if (lb->state == LB_RECORDING) {
        // `filled` deliberately survives: it is the playback length the
        // caller is about to use. It's cleared by the NEXT start, not here.
        lb->state = LB_PLAYING;
        return LB_ACT_PLAYBACK;
    }
    if (lb->state == LB_IDLE) {
        lb->filled = 0;
        lb->state = LB_RECORDING;
        return LB_ACT_START_REC;
    }
    return LB_ACT_NONE;   // pressing during playback does nothing
}

lb_action_t lb_reserve(lb_t *lb, int avail, int *offset, int *take) {
    *offset = lb->filled;
    *take = 0;
    if (lb->state != LB_RECORDING) {
        *offset = 0;
        return LB_ACT_NONE;
    }
    int room = lb->capacity - lb->filled;
    *take = (avail < room) ? avail : room;
    lb->filled += *take;
    // `*take > 0` is what makes this a one-shot: the frame that lands on the
    // cap reports it, and any later frame reserves nothing and stays quiet.
    if (*take > 0 && lb->filled >= lb->capacity) {
        lb->state = LB_PLAYING;
        return LB_ACT_PLAYBACK;
    }
    return LB_ACT_NONE;
}

void lb_playback_done(lb_t *lb) {
    lb->state = LB_IDLE;
}
