#include "gfx.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define W 16
#define H 16
static uint16_t buf[W * H];

static void clear(uint16_t v) { for (int i = 0; i < W * H; i++) buf[i] = v; }
static uint16_t px(int x, int y) { return buf[y * W + x]; }

static void test_fill_rect_paints_inside_only(void) {
    clear(0x0000);
    gfx_fill_rect(buf, W, H, 2, 3, 4, 5, 0xFFFF);
    CHECK(px(2, 3) == 0xFFFF);
    CHECK(px(5, 7) == 0xFFFF);   // last painted pixel: x in [2,6), y in [3,8)
    CHECK(px(6, 3) == 0x0000);  // just past right edge
    CHECK(px(2, 8) == 0x0000);  // just past bottom edge
    CHECK(px(1, 3) == 0x0000);  // just before left edge
}

static void test_fill_rect_clips_to_buffer(void) {
    clear(0x0000);
    // starts before the buffer and extends past it on both axes
    gfx_fill_rect(buf, W, H, -2, -2, 6, 6, 0xFFFF);
    CHECK(px(0, 0) == 0xFFFF);
    CHECK(px(3, 3) == 0xFFFF);
    CHECK(px(4, 4) == 0x0000);  // -2+6=4 -> last painted col/row is 3

    clear(0x0000);
    gfx_fill_rect(buf, W, H, W - 2, H - 2, 6, 6, 0xFFFF);
    CHECK(px(W - 1, H - 1) == 0xFFFF);  // clipped at buffer edge, no crash
}

static void test_fill_rect_fully_outside_is_noop(void) {
    clear(0x1234);
    gfx_fill_rect(buf, W, H, 100, 100, 5, 5, 0xFFFF);
    for (int i = 0; i < W * H; i++) CHECK(buf[i] == 0x1234);
}

static void test_fill_circle_paints_center_and_edges(void) {
    clear(0x0000);
    gfx_fill_circle(buf, W, H, 8, 8, 3, 0xFFFF);
    CHECK(px(8, 8) == 0xFFFF);   // center
    CHECK(px(8, 5) == 0xFFFF);   // top edge (r=3)
    CHECK(px(8, 11) == 0xFFFF);  // bottom edge
    CHECK(px(8, 4) == 0x0000);   // just outside top
    CHECK(px(0, 0) == 0x0000);   // far corner untouched
}

static void test_fill_circle_clips_to_buffer(void) {
    clear(0x0000);
    // center at the very corner — most of the circle is off-buffer
    gfx_fill_circle(buf, W, H, 0, 0, 3, 0xFFFF);
    CHECK(px(0, 0) == 0xFFFF);
    CHECK(px(2, 0) == 0xFFFF);
    // must not crash / corrupt memory outside buf (nothing to assert
    // directly here beyond "test harness didn't segfault")
}

int main(void) {
    test_fill_rect_paints_inside_only();
    test_fill_rect_clips_to_buffer();
    test_fill_rect_fully_outside_is_noop();
    test_fill_circle_paints_center_and_edges();
    test_fill_circle_clips_to_buffer();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
