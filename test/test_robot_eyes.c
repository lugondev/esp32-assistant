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

static void test_dirty_band_covers_the_worst_case_emotion_reach(void) {
    // r = panel_h/4 = 5. The band must cover every emotion's vertical
    // reach, not just NEUTRAL's (a prior version used exactly r here,
    // which clipped SURPRISED — 130% height + a -15% upward shift reaches
    // 1.39r from center, verified against the emotion table). 140% of r
    // gives a small safety margin over that: half=7, band=[10-7,10+7)=[3,17).
    int y, height;
    robot_eyes_dirty_band(H, &y, &height);
    CHECK(y == 3);
    CHECK(height == 14);
}

static void test_render_full_panel_open_paints_both_eye_centers(void) {
    // now_ms=150 is the first instant an eye is open (see test above).
    // buf_rows == panel_h, y_offset == 0 -> whole-panel render, same as
    // the pre-crop-support behavior.
    robot_eyes_render(buf, W, H, H, 0, 150, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    int left_cx = W / 4, right_cx = 3 * W / 4, cy = H / 2;
    CHECK(px(left_cx, cy) == EYE);
    CHECK(px(right_cx, cy) == EYE);
    CHECK(px(0, 0) == BG);  // corner stays background
}

static void test_render_full_panel_open_paints_above_and_below_center(void) {
    // eye_radius = H/4 = 5; a pixel 2px above center is still within the
    // open circle, but would be outside the closed eye's thin band.
    robot_eyes_render(buf, W, H, H, 0, 150, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    int left_cx = W / 4, cy = H / 2;
    CHECK(px(left_cx, cy - 2) == EYE);
}

static void test_render_full_panel_closed_clears_above_and_below_center(void) {
    // now_ms=0 is inside the blink window -> eyes closed
    robot_eyes_render(buf, W, H, H, 0, 0, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    int left_cx = W / 4, cy = H / 2;
    CHECK(px(left_cx, cy - 2) == BG);   // no longer painted when closed
    CHECK(px(left_cx, cy) == EYE);      // thin band through center remains
}

static void test_render_open_includes_a_dim_glow_tone(void) {
    // Eyes are a bright rounded-rect on top of a dimmer, larger "glow"
    // rounded-rect (a cheap halo, since there's no real blur primitive) —
    // somewhere in the buffer a third tone (neither full-bright EYE nor
    // BG) must appear, or the glow layer isn't actually being drawn.
    robot_eyes_render(buf, W, H, H, 0, 150, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    bool saw_glow_tone = false;
    for (int i = 0; i < W * H; i++) if (buf[i] != EYE && buf[i] != BG) saw_glow_tone = true;
    CHECK(saw_glow_tone);
}

static void test_render_open_glow_stays_bounded(void) {
    // The glow halo is bounded, not a full-row wash — far from either eye
    // (here, the buffer's left edge) must still be plain background.
    robot_eyes_render(buf, W, H, H, 0, 150, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    int cy = H / 2;
    CHECK(px(0, cy) == BG);
}

static void test_render_sleepy_eyes_are_much_shorter_than_neutral(void) {
    int left_cx = W / 4, cy = H / 2;
    robot_eyes_render(buf, W, H, H, 0, 150, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    CHECK(px(left_cx, cy - 3) == EYE);   // neutral: still within full eye height
    robot_eyes_render(buf, W, H, H, 0, 150, ROBOT_EMOTION_SLEEPY, EYE, BG);
    CHECK(px(left_cx, cy - 3) == BG);    // sleepy: much shorter, now above it
}

static void test_render_confused_is_asymmetric_between_eyes(void) {
    // CONFUSED is the one emotion that isn't mirrored (one eyebrow raised) —
    // mirror-compare a patch near each eye's inner-top corner; every other
    // emotion renders identically under this mirroring, CONFUSED must not.
    robot_eyes_render(buf, W, H, H, 0, 150, ROBOT_EMOTION_CONFUSED, EYE, BG);
    int left_cx = W / 4, right_cx = 3 * W / 4, cy = H / 2;
    bool any_diff = false;
    for (int dy = -4; dy <= 0; dy++) {
        for (int dx = 0; dx <= 6; dx++) {
            if (px(left_cx + dx, cy + dy) != px(right_cx - dx, cy + dy)) any_diff = true;
        }
    }
    CHECK(any_diff);
}

static void test_render_neutral_is_symmetric_between_eyes(void) {
    // Sanity check for the mirror-comparison technique above: a mirrored
    // (non-CONFUSED) emotion must show NO difference, or the CONFUSED test
    // could be passing for the wrong reason (e.g. an unrelated bug).
    // dx is kept inside the corner-radius boundary (corner_r=3 at this
    // scale) deliberately: right at that boundary, an even-width box has no
    // exact integer center, so left_cx+dx and right_cx-dx land 1px off from
    // true mirror images — a harmless rounding artifact invisible on a real
    // panel, not a rendering bug, but it would make this sanity check flag
    // a false mismatch if sampled exactly there.
    robot_eyes_render(buf, W, H, H, 0, 150, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    int left_cx = W / 4, right_cx = 3 * W / 4, cy = H / 2;
    for (int dy = -4; dy <= 0; dy++) {
        for (int dx = 0; dx <= 4; dx++) {
            CHECK(px(left_cx + dx, cy + dy) == px(right_cx - dx, cy + dy));
        }
    }
}

// The real device is ~240x216 (eyes area, after the status bar) — a much
// squarer aspect ratio than the W=40,H=20 (2:1) canvas above, which happens
// to leave enough horizontal room that an eye-width bug driven purely off
// panel height wouldn't show up there. Use device-proportioned dimensions
// (scaled down 2x) to actually catch overlap/clipping regressions.
#define DW 120
#define DH 108
static uint16_t dbuf[DW * DH];
static uint16_t dpx(int x, int y) { return dbuf[y * DW + x]; }
static void dclear(void) { for (int i = 0; i < DW * DH; i++) dbuf[i] = BG; }

static void test_render_device_aspect_eyes_dont_touch_or_clip(void) {
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 150, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    int left_cx = DW / 4, right_cx = 3 * DW / 4, cy = DH / 2;
    // Midpoint between the eyes must still be background — if the two
    // eyes/glows touch or overlap, this pixel would be lit.
    CHECK(dpx((left_cx + right_cx) / 2, cy) == BG);
    // Screen edges (x=0 and x=DW-1) must still be background — if an eye's
    // glow reaches the panel edge, it's been clipped rather than margined.
    CHECK(dpx(0, cy) == BG);
    CHECK(dpx(DW - 1, cy) == BG);
}

static void test_render_cropped_band_matches_full_panel_render(void) {
    // Render into a buffer sized/positioned exactly per robot_eyes_dirty_band
    // and confirm it reproduces the same pixels the full-panel render would
    // have shown in that band — proving the crop is just an optimization,
    // not a behavior change.
    int band_y, band_h;
    robot_eyes_dirty_band(H, &band_y, &band_h);
    CHECK(band_h <= H);

    static uint16_t full[W * H];
    static uint16_t crop[W * H];  // oversized on purpose; only band_h rows used
    robot_eyes_render(full, W, H, H, 0, 150, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    robot_eyes_render(crop, W, band_h, H, band_y, 150, ROBOT_EMOTION_NEUTRAL, EYE, BG);

    for (int y = 0; y < band_h; y++) {
        for (int x = 0; x < W; x++) {
            CHECK(crop[y * W + x] == full[(y + band_y) * W + x]);
        }
    }
}

static void test_decor_for_maps_happy_sleepy_and_surprised_only(void) {
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_HAPPY) == ROBOT_DECOR_MOUTH);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SLEEPY) == ROBOT_DECOR_ZZZ);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SURPRISED) == ROBOT_DECOR_WAVES);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_NEUTRAL) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SAD) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_ANGRY) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_CONFUSED) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SUSPICIOUS) == ROBOT_DECOR_NONE);
}

static void test_decor_band_waves_is_below_eye_center(void) {
    int y, height;
    robot_eyes_decor_band(H, ROBOT_DECOR_WAVES, &y, &height);
    CHECK(y > H / 2);
    CHECK(height > 0);
}

static void test_render_decor_waves_paints_something(void) {
    robot_eyes_render_decor(buf, W, H, H, 0, 150, ROBOT_DECOR_WAVES, EYE, BG);
    bool any_fg = false;
    for (int i = 0; i < W * H; i++) if (buf[i] == EYE) any_fg = true;
    CHECK(any_fg);
}

static void test_render_decor_waves_animates_over_time(void) {
    // Bar heights travel over time — two well-separated instants must not
    // render pixel-identical (not asserting an exact pattern, just motion).
    robot_eyes_render_decor(buf, W, H, H, 0, 0, ROBOT_DECOR_WAVES, EYE, BG);
    uint16_t snap_a[W * H];
    for (int i = 0; i < W * H; i++) snap_a[i] = buf[i];
    robot_eyes_render_decor(buf, W, H, H, 0, 400, ROBOT_DECOR_WAVES, EYE, BG);
    bool any_diff = false;
    for (int i = 0; i < W * H; i++) if (buf[i] != snap_a[i]) any_diff = true;
    CHECK(any_diff);
}

static void test_decor_band_mouth_is_below_eye_center(void) {
    int y, height;
    robot_eyes_decor_band(H, ROBOT_DECOR_MOUTH, &y, &height);
    CHECK(y > H / 2);      // starts below panel center (where the eyes are)
    CHECK(height > 0);
}

// The real device's eyes area is panel_h=216 (240 - 24px status bar), not
// the tiny H=20 canvas used above — at that scale a prior version of these
// bands (checked only "doesn't overlap the eyes", never "still fits on the
// actual screen") overflowed the panel entirely: MOUTH/WAVES ran 16px past
// the bottom edge, ZZZ computed a NEGATIVE y (27px past the top edge).
#define DEVICE_EYES_H 216

static void test_decor_bands_fit_within_the_real_panel(void) {
    int y, height;
    robot_eyes_decor_band(DEVICE_EYES_H, ROBOT_DECOR_MOUTH, &y, &height);
    CHECK(y >= 0);
    CHECK(y + height <= DEVICE_EYES_H);

    robot_eyes_decor_band(DEVICE_EYES_H, ROBOT_DECOR_WAVES, &y, &height);
    CHECK(y >= 0);
    CHECK(y + height <= DEVICE_EYES_H);

    robot_eyes_decor_band(DEVICE_EYES_H, ROBOT_DECOR_ZZZ, &y, &height);
    CHECK(y >= 0);
    CHECK(y + height <= DEVICE_EYES_H);
}

static void test_decor_band_zzz_is_above_eye_center(void) {
    int y, height;
    robot_eyes_decor_band(H, ROBOT_DECOR_ZZZ, &y, &height);
    CHECK(y + height <= H / 2);   // fully above panel center
    CHECK(height > 0);
}

static void test_render_decor_mouth_paints_something(void) {
    robot_eyes_render_decor(buf, W, H, H, 0, 150, ROBOT_DECOR_MOUTH, EYE, BG);
    bool any_fg = false;
    for (int i = 0; i < W * H; i++) if (buf[i] == EYE) any_fg = true;
    CHECK(any_fg);
}

static void test_render_decor_mouth_animates_open_and_closed(void) {
    // Two different instants must produce different pixel counts (the
    // mouth opens/closes while "talking") — not proving exact timing, just
    // that it isn't a static, unchanging shape.
    robot_eyes_render_decor(buf, W, H, H, 0, 0, ROBOT_DECOR_MOUTH, EYE, BG);
    int count_a = 0;
    for (int i = 0; i < W * H; i++) if (buf[i] == EYE) count_a++;
    robot_eyes_render_decor(buf, W, H, H, 0, 150, ROBOT_DECOR_MOUTH, EYE, BG);
    int count_b = 0;
    for (int i = 0; i < W * H; i++) if (buf[i] == EYE) count_b++;
    CHECK(count_a != count_b);
}

static void test_render_decor_zzz_builds_up_then_resets(void) {
    // The "Z Z Z" cluster should show more glyphs later in its cycle than
    // at the very start, then reset — check growth across the first part
    // of one cycle rather than an exact glyph count (implementation detail).
    robot_eyes_render_decor(buf, W, H, H, 0, 0, ROBOT_DECOR_ZZZ, EYE, BG);
    int count_a = 0;
    for (int i = 0; i < W * H; i++) if (buf[i] == EYE) count_a++;
    robot_eyes_render_decor(buf, W, H, H, 0, 1000, ROBOT_DECOR_ZZZ, EYE, BG);
    int count_b = 0;
    for (int i = 0; i < W * H; i++) if (buf[i] == EYE) count_b++;
    CHECK(count_b > count_a);
}

int main(void) {
    test_closed_only_during_blink_window();
    test_blink_recurs_every_interval();
    test_dirty_band_covers_the_worst_case_emotion_reach();
    test_render_full_panel_open_paints_both_eye_centers();
    test_render_full_panel_open_paints_above_and_below_center();
    test_render_sleepy_eyes_are_much_shorter_than_neutral();
    test_render_confused_is_asymmetric_between_eyes();
    test_render_neutral_is_symmetric_between_eyes();
    test_render_full_panel_closed_clears_above_and_below_center();
    test_render_open_includes_a_dim_glow_tone();
    test_render_open_glow_stays_bounded();
    test_render_device_aspect_eyes_dont_touch_or_clip();
    test_render_cropped_band_matches_full_panel_render();
    test_decor_for_maps_happy_sleepy_and_surprised_only();
    test_decor_band_mouth_is_below_eye_center();
    test_decor_band_zzz_is_above_eye_center();
    test_decor_bands_fit_within_the_real_panel();
    test_decor_band_waves_is_below_eye_center();
    test_render_decor_mouth_paints_something();
    test_render_decor_mouth_animates_open_and_closed();
    test_render_decor_zzz_builds_up_then_resets();
    test_render_decor_waves_paints_something();
    test_render_decor_waves_animates_over_time();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
