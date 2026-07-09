#include "robot_eyes.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_closed_only_during_blink_window(void) {
    CHECK(robot_eyes_is_closed(0) == true);
    CHECK(robot_eyes_is_closed(149) == true);
    CHECK(robot_eyes_is_closed(150) == false);
    CHECK(robot_eyes_is_closed(2999) == false);
}

static void test_blink_recurs_every_interval(void) {
    // second blink starts at exactly one interval later
    CHECK(robot_eyes_is_closed(3000) == true);
    CHECK(robot_eyes_is_closed(3149) == true);
    CHECK(robot_eyes_is_closed(3150) == false);
    // arbitrary later cycle (10th interval)
    CHECK(robot_eyes_is_closed(30000) == true);
    CHECK(robot_eyes_is_closed(30150) == false);
}

#define W 40
#define H 20
static uint16_t buf[W * H];
#define EYE 0xFFFF
#define BG  0x0000

static uint16_t px(int x, int y) { return buf[y * W + x]; }

static void test_render_open_paints_both_eye_centers(void) {
    // now_ms=150 is the first instant an eye is open (see test above)
    robot_eyes_render(buf, W, H, 150, EYE, BG);
    int left_cx = W / 4, right_cx = 3 * W / 4, cy = H / 2;
    CHECK(px(left_cx, cy) == EYE);
    CHECK(px(right_cx, cy) == EYE);
    CHECK(px(0, 0) == BG);  // corner stays background
}

static void test_render_open_paints_above_and_below_center(void) {
    // eye_radius = H/4 = 5; a pixel 2px above center is still within the
    // open circle, but would be outside the closed eye's thin band.
    robot_eyes_render(buf, W, H, 150, EYE, BG);
    int left_cx = W / 4, cy = H / 2;
    CHECK(px(left_cx, cy - 2) == EYE);
}

static void test_render_closed_clears_above_and_below_center(void) {
    // now_ms=0 is inside the blink window -> eyes closed
    robot_eyes_render(buf, W, H, 0, EYE, BG);
    int left_cx = W / 4, cy = H / 2;
    CHECK(px(left_cx, cy - 2) == BG);   // no longer painted when closed
    CHECK(px(left_cx, cy) == EYE);      // thin band through center remains
}

int main(void) {
    test_closed_only_during_blink_window();
    test_blink_recurs_every_interval();
    test_render_open_paints_both_eye_centers();
    test_render_open_paints_above_and_below_center();
    test_render_closed_clears_above_and_below_center();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
