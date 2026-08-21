#include "button_hold_logic.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// Simulates `n_ticks` ticks of continuous hold, returning the last non-NONE
// event seen (or NONE if none fired).
static btn_hold_event_t hold_for(int n_ticks, bool confirm_mode, btn_hold_state_t *st) {
    btn_hold_event_t last = BTN_HOLD_EVENT_NONE;
    for (int i = 0; i < n_ticks; i++) {
        btn_hold_event_t ev = btn_hold_step(false, confirm_mode, st);
        if (ev != BTN_HOLD_EVENT_NONE) last = ev;
    }
    return last;
}

// ---- normal mode: the 10s / 15s ladder ----

static void test_short_tap_reports_release(void) {
    btn_hold_state_t st = {0};
    CHECK(hold_for(5, false, &st) == BTN_HOLD_EVENT_NONE);   // 100ms
    CHECK(btn_hold_step(true, false, &st) == BTN_HOLD_EVENT_RELEASE);
}

static void test_setup_warning_fires_exactly_at_10s(void) {
    btn_hold_state_t st = {0};
    CHECK(hold_for(BTN_HOLD_SETUP_TICKS - 1, false, &st) == BTN_HOLD_EVENT_NONE);
    CHECK(btn_hold_step(false, false, &st) == BTN_HOLD_EVENT_WARN_SETUP);
}

static void test_setup_warning_does_not_refire(void) {
    btn_hold_state_t st = {0};
    hold_for(BTN_HOLD_SETUP_TICKS, false, &st);
    // Everything between the two thresholds is quiet.
    CHECK(hold_for(BTN_HOLD_ERASE_TICKS - BTN_HOLD_SETUP_TICKS - 1, false, &st)
          == BTN_HOLD_EVENT_NONE);
}

static void test_erase_warning_fires_exactly_at_15s(void) {
    btn_hold_state_t st = {0};
    hold_for(BTN_HOLD_ERASE_TICKS - 1, false, &st);
    CHECK(btn_hold_step(false, false, &st) == BTN_HOLD_EVENT_WARN_ERASE);
}

static void test_holding_past_15s_is_quiet(void) {
    btn_hold_state_t st = {0};
    hold_for(BTN_HOLD_ERASE_TICKS, false, &st);
    CHECK(hold_for(500, false, &st) == BTN_HOLD_EVENT_NONE);  // +10s
}

static void test_release_between_10s_and_15s_requests_setup(void) {
    btn_hold_state_t st = {0};
    hold_for(BTN_HOLD_SETUP_TICKS, false, &st);
    CHECK(btn_hold_step(true, false, &st) == BTN_HOLD_EVENT_SETUP);

    // ...and one tick short of 15s still means setup, not erase.
    btn_hold_state_t st2 = {0};
    hold_for(BTN_HOLD_ERASE_TICKS - 1, false, &st2);
    CHECK(btn_hold_step(true, false, &st2) == BTN_HOLD_EVENT_SETUP);
}

static void test_release_at_or_after_15s_arms_erase(void) {
    btn_hold_state_t st = {0};
    hold_for(BTN_HOLD_ERASE_TICKS, false, &st);
    CHECK(btn_hold_step(true, false, &st) == BTN_HOLD_EVENT_ERASE_ARM);
}

static void test_release_resets_state_for_the_next_press(void) {
    btn_hold_state_t st = {0};
    hold_for(BTN_HOLD_ERASE_TICKS, false, &st);
    btn_hold_step(true, false, &st);              // ERASE_ARM, state resets
    CHECK(st.ticks == 0);
    CHECK(st.warned_setup == false);
    CHECK(st.warned_erase == false);
    // A short tap right after must read as a plain tap, not a leftover hold.
    CHECK(hold_for(5, false, &st) == BTN_HOLD_EVENT_NONE);
    CHECK(btn_hold_step(true, false, &st) == BTN_HOLD_EVENT_RELEASE);
}

// ---- confirm mode: a single 3s threshold ----

static void test_confirm_hold_fires_exactly_at_3s(void) {
    btn_hold_state_t st = {0};
    CHECK(hold_for(BTN_HOLD_CONFIRM_TICKS - 1, true, &st) == BTN_HOLD_EVENT_NONE);
    CHECK(btn_hold_step(false, true, &st) == BTN_HOLD_EVENT_ERASE_CONFIRMED);
}

static void test_confirm_short_press_aborts(void) {
    btn_hold_state_t st = {0};
    hold_for(BTN_HOLD_CONFIRM_TICKS - 1, true, &st);
    CHECK(btn_hold_step(true, true, &st) == BTN_HOLD_EVENT_CONFIRM_ABORT);
}

static void test_confirm_release_after_confirming_is_quiet(void) {
    btn_hold_state_t st = {0};
    hold_for(BTN_HOLD_CONFIRM_TICKS, true, &st);   // ERASE_CONFIRMED fired
    CHECK(btn_hold_step(true, true, &st) == BTN_HOLD_EVENT_NONE);
}

static void test_confirm_hold_never_warns(void) {
    btn_hold_state_t st = {0};
    // Well past both normal thresholds, but in confirm mode: the only event is
    // the 3s confirmation, never WARN_SETUP/WARN_ERASE.
    btn_hold_event_t last = hold_for(BTN_HOLD_ERASE_TICKS + 100, true, &st);
    CHECK(last == BTN_HOLD_EVENT_ERASE_CONFIRMED);
}

static void test_confirm_threshold_is_shorter_than_setup(void) {
    // Guards the ladder's ordering: a confirm must be reachable long before
    // the setup warning would have fired, or the UX contradicts the prompt.
    CHECK(BTN_HOLD_CONFIRM_TICKS < BTN_HOLD_SETUP_TICKS);
    CHECK(BTN_HOLD_SETUP_TICKS < BTN_HOLD_ERASE_TICKS);
}

int main(void) {
    test_short_tap_reports_release();
    test_setup_warning_fires_exactly_at_10s();
    test_setup_warning_does_not_refire();
    test_erase_warning_fires_exactly_at_15s();
    test_holding_past_15s_is_quiet();
    test_release_between_10s_and_15s_requests_setup();
    test_release_at_or_after_15s_arms_erase();
    test_release_resets_state_for_the_next_press();
    test_confirm_hold_fires_exactly_at_3s();
    test_confirm_short_press_aborts();
    test_confirm_release_after_confirming_is_quiet();
    test_confirm_hold_never_warns();
    test_confirm_threshold_is_shorter_than_setup();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
