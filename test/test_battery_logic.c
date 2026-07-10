#include "battery_logic.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_pct_clamps_at_full_and_empty(void) {
    CHECK(battery_pct_from_millivolts(4200) == 100);
    CHECK(battery_pct_from_millivolts(4500) == 100);  // above pack max -> clamp
    CHECK(battery_pct_from_millivolts(3300) == 0);
    CHECK(battery_pct_from_millivolts(0) == 0);        // below pack min -> clamp
}

static void test_pct_interpolates_between_table_points(void) {
    // Table has exact points at 3300mV=0% and 3610mV=10%; the midpoint
    // voltage should land close to the midpoint percentage.
    int mid = battery_pct_from_millivolts(3455);  // (3300+3610)/2
    CHECK(mid >= 3 && mid <= 7);
}

static void test_pct_is_monotonic_non_decreasing(void) {
    int prev = battery_pct_from_millivolts(3000);
    for (int mv = 3000; mv <= 4300; mv += 25) {
        int pct = battery_pct_from_millivolts(mv);
        CHECK(pct >= prev);
        prev = pct;
    }
}

static void test_charge_state_charging(void) {
    CHECK(battery_charge_state_from_pins(/*chrg_active=*/true, /*stdby_active=*/false) == BATTERY_CHARGING);
}

static void test_charge_state_full(void) {
    CHECK(battery_charge_state_from_pins(false, true) == BATTERY_FULL);
}

static void test_charge_state_not_charging_when_both_inactive(void) {
    CHECK(battery_charge_state_from_pins(false, false) == BATTERY_NOT_CHARGING);
}

static void test_charge_state_not_charging_when_both_active_is_treated_defensively(void) {
    // Shouldn't happen per the TP4056 datasheet (mutually exclusive pins);
    // must not report a false CHARGING/FULL if it somehow does.
    CHECK(battery_charge_state_from_pins(true, true) == BATTERY_NOT_CHARGING);
}

int main(void) {
    test_pct_clamps_at_full_and_empty();
    test_pct_interpolates_between_table_points();
    test_pct_is_monotonic_non_decreasing();
    test_charge_state_charging();
    test_charge_state_full();
    test_charge_state_not_charging_when_both_inactive();
    test_charge_state_not_charging_when_both_active_is_treated_defensively();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
