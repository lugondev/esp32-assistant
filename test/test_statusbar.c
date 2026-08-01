#include "statusbar.h"
#include "display_font.h"
#include "wifi_signal.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// The shared ladder both the HUD and the setup portal read from. Never 0:
// a reading exists means the link exists (see wifi_signal.h).
static void test_signal_bars_ladder(void) {
    CHECK(wifi_signal_bars(-30) == 4);
    CHECK(wifi_signal_bars(-55) == 4);
    CHECK(wifi_signal_bars(-56) == 3);
    CHECK(wifi_signal_bars(-65) == 3);
    CHECK(wifi_signal_bars(-66) == 2);
    CHECK(wifi_signal_bars(-75) == 2);
    CHECK(wifi_signal_bars(-76) == 1);
    CHECK(wifi_signal_bars(-95) == 1);
}

// The status bar's own contribution on top of that ladder: a disconnected
// radio shows no bars at all, whatever stale RSSI is lying around.
static void test_wifi_bars_disconnected_is_zero(void) {
    CHECK(statusbar_wifi_bars(false, -40) == 0);
}

static void test_wifi_bars_thresholds(void) {
    CHECK(statusbar_wifi_bars(true, -50) == 4);
    CHECK(statusbar_wifi_bars(true, -55) == 4);
    CHECK(statusbar_wifi_bars(true, -60) == 3);
    CHECK(statusbar_wifi_bars(true, -65) == 3);
    CHECK(statusbar_wifi_bars(true, -70) == 2);
    CHECK(statusbar_wifi_bars(true, -75) == 2);
    CHECK(statusbar_wifi_bars(true, -80) == 1);
    CHECK(statusbar_wifi_bars(true, -90) == 1);
}

#define W 240
#define H 24
static uint16_t buf[W * H];
#define FG 0xFFFF
#define BG 0x0000

static uint16_t px(int x, int y) { return buf[y * W + x]; }
static void clear(void) { for (int i = 0; i < W * H; i++) buf[i] = BG; }

static void test_render_draws_more_wifi_bars_for_more_signal(void) {
    // The 3rd bar column (index 2, 0-based) should be filled fg when
    // wifi_bars>=3, but not when wifi_bars==1 — same x/y regardless of bars
    // count, only the fill differs.
    int bar_w = 3, bar_gap = 1, bar_x0 = 4;
    int third_bar_x = bar_x0 + 2 * (bar_w + bar_gap);
    int third_bar_bottom_y = H - 3;   // 1px above the bottom margin

    clear();
    statusbar_render(buf, W, H, /*wifi_bars=*/4, "", -1, false, FG, BG);
    CHECK(px(third_bar_x, third_bar_bottom_y) == FG);

    clear();
    statusbar_render(buf, W, H, /*wifi_bars=*/1, "", -1, false, FG, BG);
    CHECK(px(third_bar_x, third_bar_bottom_y) == BG);
}

static void test_render_draws_centered_text(void) {
    // 'A' glyph's known bit pattern (see display_font.c / test_display_font.c):
    // row 0 = 0x0C -> bits 2,3 set. Confirm the glyph is actually blitted
    // somewhere in the buffer (not asserting exact x — just that centering
    // + font lookup produced real, non-background pixels for a non-space char).
    clear();
    statusbar_render(buf, W, H, 0, "A", -1, false, FG, BG);
    bool any_fg = false;
    for (int i = 0; i < W * H; i++) if (buf[i] == FG) any_fg = true;
    CHECK(any_fg);
}

static void test_render_omits_battery_when_negative(void) {
    clear();
    statusbar_render(buf, W, H, 0, "", -1, false, FG, BG);
    for (int i = 0; i < W * H; i++) CHECK(buf[i] == BG);
}

static void test_render_draws_battery_when_nonnegative(void) {
    clear();
    statusbar_render(buf, W, H, 0, "", 100, false, FG, BG);
    bool any_fg = false;
    for (int i = 0; i < W * H; i++) if (buf[i] == FG) any_fg = true;
    CHECK(any_fg);
}

static void test_render_omits_charging_bolt_when_not_charging(void) {
    clear();
    statusbar_render(buf, W, H, 0, "", -1, false, FG, BG);
    for (int i = 0; i < W * H; i++) CHECK(buf[i] == BG);
}

static void test_render_draws_charging_bolt_when_charging(void) {
    // No battery icon (-1) so any fg pixel at all must come from the bolt —
    // isolates the charging indicator from the battery-icon rendering path.
    clear();
    statusbar_render(buf, W, H, 0, "", -1, true, FG, BG);
    bool any_fg = false;
    for (int i = 0; i < W * H; i++) if (buf[i] == FG) any_fg = true;
    CHECK(any_fg);
}

int main(void) {
    test_signal_bars_ladder();
    test_wifi_bars_disconnected_is_zero();
    test_wifi_bars_thresholds();
    test_render_draws_more_wifi_bars_for_more_signal();
    test_render_draws_centered_text();
    test_render_omits_battery_when_negative();
    test_render_draws_battery_when_nonnegative();
    test_render_omits_charging_bolt_when_not_charging();
    test_render_draws_charging_bolt_when_charging();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
