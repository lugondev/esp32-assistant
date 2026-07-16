#include "robot_eyes.h"
#include <stdio.h>
#include <string.h>

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

static void test_decor_for_mapping(void) {
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_HAPPY) == ROBOT_DECOR_MOUTH);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_LAUGHING) == ROBOT_DECOR_MOUTH);
    // SLEEPY's Zzz is drawn in-band next to the eye (like drops), not as a
    // decor band — the old ZZZ decor floated a full half-panel above the
    // droopy sleepy eyes.
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SLEEPY) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SURPRISED) == ROBOT_DECOR_WAVES);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_LISTENING) == ROBOT_DECOR_WAVES);
    // representatives of the no-decor emotions
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_NEUTRAL) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SAD) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_ANGRY) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_CONFUSED) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SUSPICIOUS) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_GLEE) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_CRYING) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SQUINT) == ROBOT_DECOR_NONE);
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

static void test_is_closed_for_neutral_matches_legacy(void) {
    for (uint32_t t = 0; t < 6200; t += 37)
        CHECK(robot_eyes_is_closed(t) == robot_eyes_is_closed_for(ROBOT_EMOTION_NEUTRAL, t));
}

static void test_per_emotion_blink_schedules(void) {
    // BORED: 7000/400 — slow, stays closed long
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 0) == true);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 399) == true);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 400) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 6999) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 7000) == true);
    // ANXIOUS: 1400/100 — fast
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_ANXIOUS, 99) == true);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_ANXIOUS, 100) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_ANXIOUS, 1400) == true);
    // SQUINT: 9000/150 — hardly ever blinks
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_SQUINT, 150) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_SQUINT, 8999) == false);
}

static void test_every_emotion_paints_something_when_open(void) {
    // Catches an empty/all-zero table entry: every emotion, at an instant
    // its eyes are open, must paint at least one EYE pixel.
    for (int e = 0; e < ROBOT_EMOTION_COUNT; e++) {
        uint32_t t = 500;   // outside every entry's blink window (max duration 400)
        dclear();
        robot_eyes_render(dbuf, DW, DH, DH, 0, t, (robot_emotion_t)e, EYE, BG);
        int lit = 0;
        for (int i = 0; i < DW * DH; i++) if (dbuf[i] == EYE) lit++;
        CHECK(lit > 0);
    }
}

// Times covering the distinct phases: open/closed across the various blink
// schedules, shake (120ms cycle), bounce (600ms), oscillate (250ms), sweat
// pulse (800ms), tear slide (1200ms — deepest at ph=1199, i.e. t=5999).
static const uint32_t SAMPLE_TIMES[] = {
    0, 59, 60, 149, 150, 199, 250, 399, 500, 650, 899,
    1300, 2100, 3049, 4400, 5999, 7100, 8999
};
#define N_SAMPLE_TIMES (sizeof SAMPLE_TIMES / sizeof SAMPLE_TIMES[0])

static void test_all_emotions_stay_inside_dirty_band(void) {
    // Render the full panel; every row OUTSIDE the dirty band must be pure
    // BG for every emotion at every sampled time — an emotion (or a later
    // task's motion/drop) that overflows the band fails here instead of
    // silently clipping on the real screen.
    int band_y, band_h;
    robot_eyes_dirty_band(DH, &band_y, &band_h);
    for (int e = 0; e < ROBOT_EMOTION_COUNT; e++) {
        for (unsigned ti = 0; ti < N_SAMPLE_TIMES; ti++) {
            dclear();
            robot_eyes_render(dbuf, DW, DH, DH, 0, SAMPLE_TIMES[ti], (robot_emotion_t)e, EYE, BG);
            for (int y = 0; y < DH; y++) {
                if (y >= band_y && y < band_y + band_h) continue;
                for (int x = 0; x < DW; x++) CHECK(dpx(x, y) == BG);
            }
        }
    }
}

static void test_render_is_deterministic(void) {
    static uint16_t a[DW * DH], b[DW * DH];
    robot_eyes_render(a, DW, DH, DH, 0, 1234, ROBOT_EMOTION_LAUGHING, EYE, BG);
    robot_eyes_render(b, DW, DH, DH, 0, 1234, ROBOT_EMOTION_LAUGHING, EYE, BG);
    for (int i = 0; i < DW * DH; i++) CHECK(a[i] == b[i]);
}

static bool frames_differ(robot_emotion_t e, uint32_t t1, uint32_t t2) {
    static uint16_t a[DW * DH], b[DW * DH];
    robot_eyes_render(a, DW, DH, DH, 0, t1, e, EYE, BG);
    robot_eyes_render(b, DW, DH, DH, 0, t2, e, EYE, BG);
    for (int i = 0; i < DW * DH; i++) if (a[i] != b[i]) return true;
    return false;
}

static void test_motion_emotions_animate_while_open(void) {
    // Both instants sit outside the respective emotion's blink window.
    CHECK(frames_differ(ROBOT_EMOTION_FRUSTRATED, 150, 210));  // SHAKE: 60ms half-cycle
    CHECK(frames_differ(ROBOT_EMOTION_CONFUSED,   150, 210));  // SHAKE
    // BOUNCE: 150 vs 300, NOT 150 vs 450 — the triangle wave is symmetric
    // around its 300ms peak, so 150 and 450 quantize to the same offset.
    CHECK(frames_differ(ROBOT_EMOTION_GLEE,       150, 300));
    CHECK(frames_differ(ROBOT_EMOTION_LAUGHING,   150, 275));  // OSCILLATE: phases in 250ms cycle
    CHECK(!frames_differ(ROBOT_EMOTION_NEUTRAL,   500, 560));  // control: no motion
}

static void test_happy_family_is_top_heavy_arcs(void) {
    // bottom_cut erases the eye's lower part → EYE pixels above the panel
    // center must strongly outnumber those below. NEUTRAL (no cut) is the
    // control: roughly balanced.
    robot_emotion_t arcs[] = { ROBOT_EMOTION_HAPPY, ROBOT_EMOTION_LAUGHING, ROBOT_EMOTION_GLEE };
    for (unsigned k = 0; k < 3; k++) {
        dclear();
        robot_eyes_render(dbuf, DW, DH, DH, 0, 500, arcs[k], EYE, BG);
        int above = 0, below = 0, cy = DH / 2;
        for (int y = 0; y < DH; y++)
            for (int x = 0; x < DW / 2; x++) {
                if (dpx(x, y) != EYE) continue;
                if (y < cy) above++; else below++;
            }
        CHECK(above > 2 * below);
    }
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 500, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    int above = 0, below = 0, cy = DH / 2;
    for (int y = 0; y < DH; y++)
        for (int x = 0; x < DW / 2; x++) {
            if (dpx(x, y) != EYE) continue;
            if (y < cy) above++; else below++;
        }
    CHECK(above <= 2 * below);
}

static int count_eye_px(void) {
    int n = 0;
    for (int i = 0; i < DW * DH; i++) if (dbuf[i] == EYE) n++;
    return n;
}

static void test_sweat_drop_pulses(void) {
    // NERVOUS is open at both 150 and 650 (blink 2200/120); 150%800<500 →
    // drop visible, 650%800>=500 → hidden. The eyes themselves are
    // identical at both instants, so the pixel delta IS the sweat drop.
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 150, ROBOT_EMOTION_NERVOUS, EYE, BG);
    int with_drop = count_eye_px();
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 650, ROBOT_EMOTION_NERVOUS, EYE, BG);
    int without_drop = count_eye_px();
    CHECK(with_drop > without_drop);
}

static void test_tear_slides_down_over_time(void) {
    // CRYING is open at 300 and 900 (blink 4500/250). Different tear
    // phases → different frames, and the later phase must reach DEEPER
    // (lowest lit row sits lower).
    CHECK(frames_differ(ROBOT_EMOTION_CRYING, 300, 900));
    int lowest_a = -1, lowest_b = -1;
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 300, ROBOT_EMOTION_CRYING, EYE, BG);
    for (int y = 0; y < DH; y++) for (int x = 0; x < DW; x++) if (dpx(x, y) == EYE) lowest_a = y;
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 900, ROBOT_EMOTION_CRYING, EYE, BG);
    for (int y = 0; y < DH; y++) for (int x = 0; x < DW; x++) if (dpx(x, y) == EYE) lowest_b = y;
    CHECK(lowest_b > lowest_a);
}

static void test_asym_emotions_differ_between_eyes(void) {
    robot_emotion_t asym[] = { ROBOT_EMOTION_SKEPTICAL, ROBOT_EMOTION_SUSPICIOUS,
                               ROBOT_EMOTION_ANNOYED, ROBOT_EMOTION_UNIMPRESSED };
    for (unsigned k = 0; k < 4; k++) {
        dclear();
        robot_eyes_render(dbuf, DW, DH, DH, 0, 500, asym[k], EYE, BG);
        int left_cx = DW / 4, right_cx = 3 * DW / 4, cy = DH / 2;
        bool any_diff = false;
        for (int dy = -20; dy <= 20; dy++)
            for (int dx = 0; dx <= 20; dx++)
                if (dpx(left_cx + dx, cy + dy) != dpx(right_cx - dx, cy + dy)) any_diff = true;
        CHECK(any_diff);
    }
}

static void test_sleepy_zzz_builds_near_the_eye(void) {
    // t=400 → 0 'Z' glyphs; t=1600 → 3 glyphs (500ms steps, %4). Both
    // instants have identical, open eyes (blink 3000/300), so every pixel
    // that differs IS the Z cluster — and it must sit close to the sleepy
    // eye (within ~1.25r above its center, on the right half), not up at
    // the dirty band's top edge like the removed ZZZ decor did.
    static uint16_t none[DW * DH], three[DW * DH];
    robot_eyes_render(none,  DW, DH, DH, 0,  400, ROBOT_EMOTION_SLEEPY, EYE, BG);
    robot_eyes_render(three, DW, DH, DH, 0, 1600, ROBOT_EMOTION_SLEEPY, EYE, BG);
    int r = DH / 4;
    int eye_cy = DH / 2 + (r * 25) / 100;   // SLEEPY's y_shift_pct = 25
    int diff = 0;
    for (int y = 0; y < DH; y++) {
        for (int x = 0; x < DW; x++) {
            if (none[y * DW + x] == three[y * DW + x]) continue;
            diff++;
            CHECK(x >= DW / 2 - 5);                 // near the RIGHT eye
            CHECK(y >= eye_cy - (5 * r) / 4);       // no higher than 1.25r above it
            CHECK(y < eye_cy);                      // strictly above the eye center
        }
    }
    CHECK(diff > 0);   // the cluster actually appeared
}

static void test_emotion_names_cover_all_states(void) {
    // Every emotion needs a short display name (shown on the status bar by
    // the random-emotion button); out-of-range values fall back to "?".
    for (int e = 0; e < ROBOT_EMOTION_COUNT; e++) {
        const char *n = robot_eyes_emotion_name((robot_emotion_t)e);
        CHECK(n != NULL);
        CHECK(n[0] != '\0');
    }
    // spot-check enum<->name alignment at both ends of the table
    CHECK(robot_eyes_emotion_name(ROBOT_EMOTION_NEUTRAL)[0] == 'n');
    CHECK(robot_eyes_emotion_name(ROBOT_EMOTION_SQUINT)[0] == 's');
    CHECK(robot_eyes_emotion_name((robot_emotion_t)ROBOT_EMOTION_COUNT)[0] == '?');
}

static void test_glow_pct_default_and_clamp(void) {
    CHECK(robot_eyes_glow_pct() == ROBOT_EYES_GLOW_PCT_DEFAULT);
    robot_eyes_set_glow_pct(50);
    CHECK(robot_eyes_glow_pct() == ROBOT_EYES_GLOW_PCT_MAX);   // clamped: band budget
    robot_eyes_set_glow_pct(-3);
    CHECK(robot_eyes_glow_pct() == 0);
    robot_eyes_set_glow_pct(ROBOT_EYES_GLOW_PCT_DEFAULT);
}

static void test_glow_zero_disables_the_halo(void) {
    // With glow 0 there must be NO third tone anywhere — only pure EYE and
    // BG pixels (the halo is the only source of a dimmed tone).
    robot_eyes_set_glow_pct(0);
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 500, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    bool third_tone = false;
    for (int i = 0; i < DW * DH; i++)
        if (dbuf[i] != EYE && dbuf[i] != BG) third_tone = true;
    CHECK(!third_tone);
    robot_eyes_set_glow_pct(ROBOT_EYES_GLOW_PCT_DEFAULT);
}

static void test_frame_key_static_emotion_stable_between_blinks(void) {
    // NEUTRAL has no motion/drop: between blink edges every frame renders
    // pixel-identical, so the key must not change — this is what lets
    // status_task skip redundant SPI flushes while idle.
    uint32_t open1 = robot_eyes_frame_key(ROBOT_EMOTION_NEUTRAL, 200);
    uint32_t open2 = robot_eyes_frame_key(ROBOT_EMOTION_NEUTRAL, 2900);
    CHECK(open1 == open2);
    // ...but crossing a blink edge must change it.
    uint32_t closed = robot_eyes_frame_key(ROBOT_EMOTION_NEUTRAL, 3000);
    CHECK(closed != open1);
}

static void test_frame_key_equal_keys_render_equal_frames(void) {
    // The key's core guarantee: same key => memcmp-identical frame.
    static uint16_t a[W * H], b[W * H];
    CHECK(robot_eyes_frame_key(ROBOT_EMOTION_NEUTRAL, 200) ==
          robot_eyes_frame_key(ROBOT_EMOTION_NEUTRAL, 2900));
    robot_eyes_render(a, W, H, H, 0, 200, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    robot_eyes_render(b, W, H, H, 0, 2900, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    CHECK(memcmp(a, b, sizeof a) == 0);
}

static void test_frame_key_differs_between_emotions(void) {
    CHECK(robot_eyes_frame_key(ROBOT_EMOTION_NEUTRAL, 200) !=
          robot_eyes_frame_key(ROBOT_EMOTION_FOCUSED, 200));
}

static void test_frame_key_tracks_motion(void) {
    // CONFUSED shakes on a 120ms square wave: opposite half-periods differ
    // even though both instants are mid-open (no blink involved).
    CHECK(robot_eyes_frame_key(ROBOT_EMOTION_CONFUSED, 200) !=
          robot_eyes_frame_key(ROBOT_EMOTION_CONFUSED, 260));
}

static void test_frame_key_tracks_sleepy_zzz_steps(void) {
    // The Zzz staircase adds a glyph every 500ms — the key must change step
    // to step even though the eyes themselves hold still...
    CHECK(robot_eyes_frame_key(ROBOT_EMOTION_SLEEPY, 600) !=
          robot_eyes_frame_key(ROBOT_EMOTION_SLEEPY, 1100));
    // ...and stay stable within one step (both open, same glyph count).
    CHECK(robot_eyes_frame_key(ROBOT_EMOTION_SLEEPY, 600) ==
          robot_eyes_frame_key(ROBOT_EMOTION_SLEEPY, 900));
}

static void test_decor_frame_key_mouth_toggles(void) {
    // Mouth flaps on a 300ms cycle: two instants in the same half match,
    // open vs closed halves differ. NONE is a constant.
    CHECK(robot_eyes_decor_frame_key(ROBOT_DECOR_MOUTH, 10) ==
          robot_eyes_decor_frame_key(ROBOT_DECOR_MOUTH, 140));
    CHECK(robot_eyes_decor_frame_key(ROBOT_DECOR_MOUTH, 10) !=
          robot_eyes_decor_frame_key(ROBOT_DECOR_MOUTH, 160));
    CHECK(robot_eyes_decor_frame_key(ROBOT_DECOR_NONE, 10) ==
          robot_eyes_decor_frame_key(ROBOT_DECOR_NONE, 999));
}

static void test_decor_frame_key_waves_always_animate(void) {
    // WAVES bars follow continuous triangle waves — every frame differs.
    CHECK(robot_eyes_decor_frame_key(ROBOT_DECOR_WAVES, 100) !=
          robot_eyes_decor_frame_key(ROBOT_DECOR_WAVES, 220));
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
    test_decor_for_mapping();
    test_decor_band_mouth_is_below_eye_center();
    test_decor_bands_fit_within_the_real_panel();
    test_decor_band_waves_is_below_eye_center();
    test_render_decor_mouth_paints_something();
    test_render_decor_mouth_animates_open_and_closed();
    test_render_decor_waves_paints_something();
    test_render_decor_waves_animates_over_time();
    test_is_closed_for_neutral_matches_legacy();
    test_per_emotion_blink_schedules();
    test_every_emotion_paints_something_when_open();
    test_all_emotions_stay_inside_dirty_band();
    test_render_is_deterministic();
    test_asym_emotions_differ_between_eyes();
    test_happy_family_is_top_heavy_arcs();
    test_motion_emotions_animate_while_open();
    test_sweat_drop_pulses();
    test_tear_slides_down_over_time();
    test_sleepy_zzz_builds_near_the_eye();
    test_glow_pct_default_and_clamp();
    test_glow_zero_disables_the_halo();
    test_emotion_names_cover_all_states();
    test_frame_key_static_emotion_stable_between_blinks();
    test_frame_key_equal_keys_render_equal_frames();
    test_frame_key_differs_between_emotions();
    test_frame_key_tracks_motion();
    test_frame_key_tracks_sleepy_zzz_steps();
    test_decor_frame_key_mouth_toggles();
    test_decor_frame_key_waves_always_animate();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
