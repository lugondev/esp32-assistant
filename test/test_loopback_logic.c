#include "loopback_logic.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// A 16kHz mono take of 1000 samples = 62.5ms, which is enough room for one
// full 960-sample frame plus a partial one — the exact shape the cap has to
// handle on hardware, at a size that stays readable in a test.
#define CAP 1000
#define FRAME 960

static void test_starts_idle(void) {
    lb_t lb;
    lb_init(&lb, CAP);
    CHECK(lb.state == LB_IDLE);
    CHECK(lb.filled == 0);
    CHECK(lb.capacity == CAP);
}

static void test_press_while_idle_starts_recording(void) {
    lb_t lb;
    lb_init(&lb, CAP);
    CHECK(lb_on_press(&lb) == LB_ACT_START_REC);
    CHECK(lb.state == LB_RECORDING);
}

static void test_press_while_recording_asks_for_playback(void) {
    lb_t lb;
    lb_init(&lb, CAP);
    lb_on_press(&lb);
    CHECK(lb_on_press(&lb) == LB_ACT_PLAYBACK);
    CHECK(lb.state == LB_PLAYING);
}

// The cap path has to leave the same state as the stop-press path, or the
// caller's status line says "REC" through the whole playback.
static void test_hitting_the_cap_also_enters_playing_state(void) {
    lb_t lb;
    lb_init(&lb, FRAME);
    lb_on_press(&lb);
    int offset = -1, take = -1;
    CHECK(lb_reserve(&lb, FRAME, &offset, &take) == LB_ACT_PLAYBACK);
    CHECK(lb.state == LB_PLAYING);
}

static void test_reserve_is_a_noop_while_playing(void) {
    lb_t lb;
    lb_init(&lb, CAP);
    lb_on_press(&lb);
    int offset = -1, take = -1;
    lb_reserve(&lb, FRAME, &offset, &take);
    lb_on_press(&lb);                                  // now playing
    CHECK(lb_reserve(&lb, FRAME, &offset, &take) == LB_ACT_NONE);
    CHECK(take == 0);
    CHECK(lb.filled == FRAME);   // playback length must survive a stray read
}

static void test_reserve_is_a_noop_while_idle(void) {
    lb_t lb;
    lb_init(&lb, CAP);
    int offset = -1, take = -1;
    CHECK(lb_reserve(&lb, FRAME, &offset, &take) == LB_ACT_NONE);
    CHECK(take == 0);
    CHECK(offset == 0);
    CHECK(lb.filled == 0);   // an idle reserve must not advance the buffer
}

static void test_reserve_takes_a_whole_frame_when_it_fits(void) {
    lb_t lb;
    lb_init(&lb, CAP);
    lb_on_press(&lb);
    int offset = -1, take = -1;
    CHECK(lb_reserve(&lb, FRAME, &offset, &take) == LB_ACT_NONE);
    CHECK(offset == 0);
    CHECK(take == FRAME);
    CHECK(lb.filled == FRAME);
}

static void test_consecutive_frames_append(void) {
    lb_t lb;
    lb_init(&lb, 10 * FRAME);
    lb_on_press(&lb);
    int offset = -1, take = -1;
    lb_reserve(&lb, FRAME, &offset, &take);
    CHECK(lb_reserve(&lb, FRAME, &offset, &take) == LB_ACT_NONE);
    CHECK(offset == FRAME);   // second frame lands after the first
    CHECK(take == FRAME);
    CHECK(lb.filled == 2 * FRAME);
}

// The buffer-overrun case this unit exists for: 40 samples of room left, a
// 960-sample frame arriving.
static void test_last_frame_is_clamped_to_the_remaining_room(void) {
    lb_t lb;
    lb_init(&lb, CAP);
    lb_on_press(&lb);
    int offset = -1, take = -1;
    lb_reserve(&lb, FRAME, &offset, &take);            // 960 of 1000
    CHECK(lb_reserve(&lb, FRAME, &offset, &take) == LB_ACT_PLAYBACK);
    CHECK(offset == FRAME);
    CHECK(take == CAP - FRAME);   // only the 40 that fit
    CHECK(lb.filled == CAP);      // never past capacity
}

static void test_filling_the_buffer_exactly_also_triggers_playback(void) {
    lb_t lb;
    lb_init(&lb, 2 * FRAME);
    lb_on_press(&lb);
    int offset = -1, take = -1;
    CHECK(lb_reserve(&lb, FRAME, &offset, &take) == LB_ACT_NONE);
    CHECK(lb_reserve(&lb, FRAME, &offset, &take) == LB_ACT_PLAYBACK);
    CHECK(take == FRAME);
    CHECK(lb.filled == 2 * FRAME);
}

// Once full, the state machine must stop accepting audio even if the caller
// keeps reading frames before it services the playback action.
static void test_reserve_after_full_takes_nothing(void) {
    lb_t lb;
    lb_init(&lb, FRAME);
    lb_on_press(&lb);
    int offset = -1, take = -1;
    lb_reserve(&lb, FRAME, &offset, &take);
    CHECK(lb_reserve(&lb, FRAME, &offset, &take) == LB_ACT_NONE);
    CHECK(take == 0);
    CHECK(lb.filled == FRAME);
}

static void test_playback_done_returns_to_idle(void) {
    lb_t lb;
    lb_init(&lb, CAP);
    lb_on_press(&lb);
    lb_on_press(&lb);
    lb_playback_done(&lb);
    CHECK(lb.state == LB_IDLE);
}

// A second take must not replay the first one's tail: filled resets on the
// press that starts it, not on the playback that ended the previous take
// (the caller still needs `filled` to know how much to play).
static void test_next_take_starts_from_an_empty_buffer(void) {
    lb_t lb;
    lb_init(&lb, CAP);
    lb_on_press(&lb);
    int offset = -1, take = -1;
    lb_reserve(&lb, FRAME, &offset, &take);
    CHECK(lb_on_press(&lb) == LB_ACT_PLAYBACK);
    CHECK(lb.filled == FRAME);   // still readable during playback
    lb_playback_done(&lb);

    CHECK(lb_on_press(&lb) == LB_ACT_START_REC);
    CHECK(lb.filled == 0);
    lb_reserve(&lb, FRAME, &offset, &take);
    CHECK(offset == 0);
}

// A press that stops a recording nobody spoke into: playing 0 samples is a
// no-op for the caller, but it must not be mistaken for "still recording".
static void test_stopping_an_empty_recording_is_still_a_playback(void) {
    lb_t lb;
    lb_init(&lb, CAP);
    lb_on_press(&lb);
    CHECK(lb_on_press(&lb) == LB_ACT_PLAYBACK);
    CHECK(lb.filled == 0);
}

// audio_mic_read() returns a short/zero count when the I2S read times out —
// which is exactly what a dead mic does, i.e. the case this whole test mode
// is built to diagnose. It must not corrupt the offset arithmetic.
static void test_short_frame_advances_by_only_what_arrived(void) {
    lb_t lb;
    lb_init(&lb, CAP);
    lb_on_press(&lb);
    int offset = -1, take = -1;
    lb_reserve(&lb, 0, &offset, &take);
    CHECK(take == 0);
    CHECK(lb.filled == 0);
    lb_reserve(&lb, 100, &offset, &take);
    CHECK(offset == 0);
    CHECK(take == 100);
    CHECK(lb.filled == 100);
}

int main(void) {
    test_starts_idle();
    test_press_while_idle_starts_recording();
    test_press_while_recording_asks_for_playback();
    test_hitting_the_cap_also_enters_playing_state();
    test_reserve_is_a_noop_while_playing();
    test_reserve_is_a_noop_while_idle();
    test_reserve_takes_a_whole_frame_when_it_fits();
    test_consecutive_frames_append();
    test_last_frame_is_clamped_to_the_remaining_room();
    test_filling_the_buffer_exactly_also_triggers_playback();
    test_reserve_after_full_takes_nothing();
    test_playback_done_returns_to_idle();
    test_next_take_starts_from_an_empty_buffer();
    test_stopping_an_empty_recording_is_still_a_playback();
    test_short_frame_advances_by_only_what_arrived();
    if (failures) { printf("%d test(s) failed\n", failures); return 1; }
    printf("all loopback_logic tests passed\n");
    return 0;
}
