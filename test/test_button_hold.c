#include "button_hold_logic.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// Simulates `n_ticks` ticks of continuous hold (released=false), returning
// the last non-NONE event seen (or NONE if none fired).
static btn_hold_event_t hold_for(int n_ticks, int *held_ticks, bool *hold_fired) {
    btn_hold_event_t last = BTN_HOLD_EVENT_NONE;
    for (int i = 0; i < n_ticks; i++) {
        btn_hold_event_t ev = btn_hold_step(false, held_ticks, hold_fired);
        if (ev != BTN_HOLD_EVENT_NONE) last = ev;
    }
    return last;
}

static void test_short_tap_releases_before_threshold(void) {
    int ticks = 0; bool fired = false;
    btn_hold_event_t during = hold_for(5, &ticks, &fired);  // 100ms held
    CHECK(during == BTN_HOLD_EVENT_NONE);
    CHECK(fired == false);
    btn_hold_event_t on_release = btn_hold_step(/*released=*/true, &ticks, &fired);
    CHECK(on_release == BTN_HOLD_EVENT_RELEASE);
}

static void test_hold_fires_exactly_at_threshold(void) {
    int ticks = 0; bool fired = false;
    // One tick short of the threshold: nothing yet.
    btn_hold_event_t during = hold_for(BTN_HOLD_THRESHOLD_TICKS - 1, &ticks, &fired);
    CHECK(during == BTN_HOLD_EVENT_NONE);
    CHECK(fired == false);
    // The threshold-crossing tick: fires HOLD exactly once.
    btn_hold_event_t at_threshold = btn_hold_step(false, &ticks, &fired);
    CHECK(at_threshold == BTN_HOLD_EVENT_HOLD);
    CHECK(fired == true);
}

static void test_hold_past_threshold_does_not_refire(void) {
    int ticks = 0; bool fired = false;
    hold_for(BTN_HOLD_THRESHOLD_TICKS, &ticks, &fired);  // crosses threshold once
    CHECK(fired == true);
    // Keep holding well past the threshold: no repeat HOLD events.
    btn_hold_event_t still_held = hold_for(100, &ticks, &fired);
    CHECK(still_held == BTN_HOLD_EVENT_NONE);
}

static void test_release_after_hold_fired_reports_nothing(void) {
    int ticks = 0; bool fired = false;
    hold_for(BTN_HOLD_THRESHOLD_TICKS, &ticks, &fired);  // HOLD already fired
    btn_hold_event_t on_release = btn_hold_step(/*released=*/true, &ticks, &fired);
    CHECK(on_release == BTN_HOLD_EVENT_NONE);
}

int main(void) {
    test_short_tap_releases_before_threshold();
    test_hold_fires_exactly_at_threshold();
    test_hold_past_threshold_does_not_refire();
    test_release_after_hold_fired_reports_nothing();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
