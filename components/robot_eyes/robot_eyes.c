#include "robot_eyes.h"
#include "gfx.h"
#include "display_font.h"

bool robot_eyes_is_closed(uint32_t now_ms) {
    return (now_ms % ROBOT_EYES_BLINK_INTERVAL_MS) < ROBOT_EYES_BLINK_DURATION_MS;
}

// Worst-case vertical reach across every emotion in EMOTIONS below, not
// just NEUTRAL — SURPRISED is the driver: height_pct=130 (half-height
// 1.3*0.8r=1.04r) plus |y_shift_pct|=15 (0.15r) plus glow_pad_y (0.2r) =
// 1.39r. 140 gives a small safety margin. Recompute this if EMOTIONS ever
// gains a more extreme height_pct/y_shift_pct combination — an emotion
// exceeding this reach will get its top/bottom clipped by the crop this
// drives, exactly the bug this constant was added to fix.
#define ROBOT_EYES_MAX_REACH_PCT 140

void robot_eyes_dirty_band(int panel_h, int *y, int *height) {
    int r  = panel_h / 4;
    int cy = panel_h / 2;
    int half = (r * ROBOT_EYES_MAX_REACH_PCT) / 100;
    *y = cy - half;
    *height = 2 * half;
}

// Halves each RGB565 channel — a cheap "dimmed" tone for the glow halo
// drawn behind each eye (no real blur primitive; layering a larger, dimmer
// rounded-rect behind a smaller bright one approximates one).
static uint16_t dim_rgb565(uint16_t c) {
    int r5 = (c >> 11) & 0x1F;
    int g6 = (c >> 5) & 0x3F;
    int b5 = c & 0x1F;
    return (uint16_t)(((r5 / 2) << 11) | ((g6 / 2) << 5) | (b5 / 2));
}

// Per-eye parametric expression knobs, all as percentages of the base
// (NEUTRAL) size — mirrors how esp32-eyes-style projects drive expressions
// off a handful of numeric parameters instead of per-emotion bitmaps.
//   height_pct/width_pct/corner_pct: scale of the base eye_h/eye_w/corner_r
//   y_shift_pct:  vertical shift as a % of r (positive = downward/droopy)
//   brow_slant_pct: 0 = no brow wedge. >0 cuts the top-OUTER corner (an
//     angry/glaring "frown", eyebrows angled down toward the nose). <0 cuts
//     the top-INNER corner (a sad/drooping brow, angled down away from the
//     nose). Magnitude (0-100) is the wedge's size as a % of the eye box.
typedef struct {
    int height_pct, width_pct, corner_pct, y_shift_pct, brow_slant_pct;
} eye_params_t;

typedef struct { eye_params_t left, right; } emotion_params_t;

// One entry per robot_emotion_t value, same order as the enum. Values are
// this project's own creative choices, not copied from any reference —
// tuned by eye (pun intended) for a readable expression at ~54px eye
// height on a 240px-wide panel; adjust freely.
static const emotion_params_t EMOTIONS[] = {
    [ROBOT_EMOTION_NEUTRAL]    = { {100,100,100,  0,   0}, {100,100,100,  0,   0} },
    [ROBOT_EMOTION_HAPPY]      = { { 75,105,140, -5,   0}, { 75,105,140, -5,   0} },
    [ROBOT_EMOTION_SAD]        = { { 85, 90, 70, 20, -40}, { 85, 90, 70, 20, -40} },
    [ROBOT_EMOTION_SURPRISED] = { {130,105,100,-15,   0}, {130,105,100,-15,   0} },
    [ROBOT_EMOTION_ANGRY]      = { { 55, 95, 25,  0,  45}, { 55, 95, 25,  0,  45} },
    [ROBOT_EMOTION_SLEEPY]     = { { 30, 90, 50, 25,   0}, { 30, 90, 50, 25,   0} },
    // Confused: one eyebrow raised, the other neutral — deliberately not
    // mirrored, unlike every other emotion here.
    [ROBOT_EMOTION_CONFUSED]   = { {100,100,100,-15, -30}, {100,100,100,  0,   0} },
    [ROBOT_EMOTION_SUSPICIOUS] = { { 45,100, 35,  8,  20}, { 45,100, 35,  8,  20} },
};

// Renders one eye (already-scaled box at ex,ey,ew,eh) plus its glow halo and
// optional brow-slant wedge. is_left selects which top corner is "outer"
// (away from the nose) vs "inner" (toward the nose) for the wedge.
static void render_eye_box(uint16_t *buf, int buf_w, int buf_rows,
                            int ex, int ey, int ew, int eh, int corner_r,
                            int glow_pad_x, int glow_pad_y, int brow_slant_pct,
                            bool is_left, uint16_t eye_color, uint16_t glow_color,
                            uint16_t bg_color) {
    int gw = ew + 2 * glow_pad_x, gh = eh + 2 * glow_pad_y;
    int gr = corner_r + glow_pad_y;
    int gx = ex - glow_pad_x, gy = ey - glow_pad_y;
    gfx_fill_rounded_rect(buf, buf_w, buf_rows, gx, gy, gw, gh, gr, glow_color);
    gfx_fill_rounded_rect(buf, buf_w, buf_rows, ex, ey, ew, eh, corner_r, eye_color);

    if (brow_slant_pct == 0) return;
    int mag = brow_slant_pct < 0 ? -brow_slant_pct : brow_slant_pct;
    if (mag > 100) mag = 100;
    int wedge_w = (ew * mag) / 100;
    int wedge_h = (eh * mag) / 200;   // shallower than wide — a brow line, not a slice
    if (wedge_w < 1 || wedge_h < 1) return;
    // angry-style (>0): cut the OUTER top corner (bg-colored wedge, so it
    // reads as a notch removed from the eye). sad-style (<0): cut the
    // INNER top corner instead. "outer" is the left edge for the left eye,
    // the right edge for the right eye (and vice versa for "inner").
    bool cut_left_side = (brow_slant_pct > 0) == is_left;
    int cx = cut_left_side ? ex : ex + ew;
    int dir = cut_left_side ? 1 : -1;   // which way the wedge extends inward
    gfx_fill_triangle(buf, buf_w, buf_rows,
                       cx, ey,
                       cx + dir * wedge_w, ey,
                       cx, ey + wedge_h,
                       bg_color);
}

void robot_eyes_render(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                        int y_offset, uint32_t now_ms, robot_emotion_t emotion,
                        uint16_t eye_color, uint16_t bg_color) {
    gfx_fill_rect(buf, buf_w, buf_rows, 0, 0, buf_w, buf_rows, bg_color);

    int r  = panel_h / 4;
    int cy0 = panel_h / 2 - y_offset;   // panel-space center, translated to buf-local rows
    int left_cx  = buf_w / 4;
    int right_cx = 3 * buf_w / 4;

    // Eye HEIGHT stays driven by r (panel_h-based, unchanged) so the
    // dirty-band-fit invariant below still holds. Eye WIDTH must instead be
    // driven by buf_w: left_cx/right_cx are both exactly buf_w/4 away from
    // the nearer screen edge AND from the midline between the eyes, so that
    // shared budget (minus a visible margin) is the hard cap each eye+glow
    // may use — deriving width from r alone ignores the panel's actual
    // aspect ratio and can overlap the other eye or clip the screen edge on
    // a squarer panel than a 2:1-ish test canvas would show.
    int slot_half_w = buf_w / 4;
    int margin_x    = slot_half_w / 6;
    int max_half_w  = slot_half_w - margin_x;
    int glow_pad_x  = max_half_w / 6;
    int base_eye_half_w = max_half_w - glow_pad_x;
    int base_eye_w = 2 * base_eye_half_w;

    int base_eye_h = (8 * r) / 5;   // 1.6r
    // glow_pad_y unchanged from before so eye_h/2 + glow_pad_y == r exactly
    // at NEUTRAL scale, keeping the vertical glow within
    // robot_eyes_dirty_band's [-r,+r) range even at emotions that enlarge
    // height_pct up to 130 (SURPRISED) — checked against the table above.
    int glow_pad_y = r / 5;
    uint16_t glow_color = dim_rgb565(eye_color);

    const emotion_params_t *em = &EMOTIONS[emotion];
    const eye_params_t *sides[2] = { &em->left, &em->right };
    int cxs[2] = { left_cx, right_cx };
    bool is_left[2] = { true, false };

    bool closed = robot_eyes_is_closed(now_ms);
    for (int i = 0; i < 2; i++) {
        const eye_params_t *p = sides[i];
        int ew = (base_eye_w * p->width_pct) / 100;
        int eh = (base_eye_h * p->height_pct) / 100;
        int corner_r = (ew * 2) / 10;   // 0.2 * ew, then further scaled below
        corner_r = (corner_r * p->corner_pct) / 100;
        int cy = cy0 + (r * p->y_shift_pct) / 100;
        int ex = cxs[i] - ew / 2, ey = cy - eh / 2;

        if (!closed) {
            render_eye_box(buf, buf_w, buf_rows, ex, ey, ew, eh, corner_r,
                            glow_pad_x, glow_pad_y, p->brow_slant_pct,
                            is_left[i], eye_color, glow_color, bg_color);
        } else {
            int band_h = eh / 5;
            if (band_h < 1) band_h = 1;
            gfx_fill_rect(buf, buf_w, buf_rows, ex, cy - band_h / 2, ew, band_h, eye_color);
        }
    }
}

robot_decor_t robot_eyes_decor_for(robot_emotion_t emotion) {
    switch (emotion) {
    case ROBOT_EMOTION_HAPPY:     return ROBOT_DECOR_MOUTH;
    case ROBOT_EMOTION_SLEEPY:    return ROBOT_DECOR_ZZZ;
    case ROBOT_EMOTION_SURPRISED: return ROBOT_DECOR_WAVES;
    default:                      return ROBOT_DECOR_NONE;
    }
}

// Offsets/heights as % of r, panel-space, from the same cy=panel_h/2 the
// eyes are centered on. panel_h is only 4r total (half-panel = 2r each
// side), and the eyes themselves already reach +-1.4r
// (ROBOT_EYES_MAX_REACH_PCT) — leaving just 0.6r of real margin on each
// side for a decor band, not the 1.0r-1.5r a prior version assumed
// (verified wrong: it overflowed the bottom edge by 16px and computed a
// NEGATIVE top y on a real 240px panel with a 24px status bar). Budget
// that 0.6r as 0.05r gap + 0.5r height + 0.05r trailing margin:
//   MOUTH/WAVES: top = 1.45r (1.4r eyes reach + 0.05r gap), height = 0.5r
//     -> ends at 1.95r, 0.05r inside the 2r bottom edge.
//   ZZZ: symmetric on the top side — ends at -1.45r, height = 0.5r
//     -> starts at -1.95r, 0.05r inside the 2r top edge.
#define ROBOT_MOUTH_TOP_PCT    145
#define ROBOT_MOUTH_HEIGHT_PCT  50
#define ROBOT_ZZZ_BOTTOM_PCT  -145
#define ROBOT_ZZZ_HEIGHT_PCT   50
// WAVES shares MOUTH's band (below the eyes) — never a conflict, since
// exactly one emotion (and so at most one decor) is active at a time.
#define ROBOT_WAVES_TOP_PCT    145
#define ROBOT_WAVES_HEIGHT_PCT  50

void robot_eyes_decor_band(int panel_h, robot_decor_t decor, int *y, int *height) {
    int r = panel_h / 4, cy = panel_h / 2;
    switch (decor) {
    case ROBOT_DECOR_MOUTH:
        *y = cy + (r * ROBOT_MOUTH_TOP_PCT) / 100;
        *height = (r * ROBOT_MOUTH_HEIGHT_PCT) / 100;
        break;
    case ROBOT_DECOR_ZZZ: {
        int bottom_edge = cy + (r * ROBOT_ZZZ_BOTTOM_PCT) / 100;
        *height = (r * ROBOT_ZZZ_HEIGHT_PCT) / 100;
        // On a small panel (e.g. a 128x64 SSD1306's ~52px eyes band) the
        // percentage above gives a band shorter than one 'Z' glyph, so
        // render_zzz's single bottommost glyph loses its top rows instead
        // of just rendering smaller. Clamp to at least a full glyph.
        if (*height < DISPLAY_FONT_GLYPH_HEIGHT) {
            *height = DISPLAY_FONT_GLYPH_HEIGHT;
            // The percentage-tuned gap above the eye leaves the clamped
            // glyph sitting right at the panel's top edge with no visual
            // gap left over the status bar — nudge it down a few px so it
            // reads as "just above the eye" instead of stuck to the edge.
            bottom_edge += 4;
        }
        *y = bottom_edge - *height;
        break;
    }
    case ROBOT_DECOR_WAVES:
        *y = cy + (r * ROBOT_WAVES_TOP_PCT) / 100;
        *height = (r * ROBOT_WAVES_HEIGHT_PCT) / 100;
        break;
    default:
        *y = 0; *height = 0;
        break;
    }
}

// Simple 2-state "talking" flap: open for the first half of each 300ms
// cycle, closed (a thin line) for the second half — not a realistic mouth,
// just enough motion to read as "speaking" at a glance.
#define ROBOT_MOUTH_CYCLE_MS 300u

static void render_mouth(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                          int y_offset, uint32_t now_ms, uint16_t fg, uint16_t bg) {
    gfx_fill_rect(buf, buf_w, buf_rows, 0, 0, buf_w, buf_rows, bg);
    int r = panel_h / 4;
    int band_top, band_h;
    robot_eyes_decor_band(panel_h, ROBOT_DECOR_MOUTH, &band_top, &band_h);
    int cy = band_top + band_h / 2 - y_offset;   // vertical center of the mouth band

    bool open = (now_ms % ROBOT_MOUTH_CYCLE_MS) < (ROBOT_MOUTH_CYCLE_MS / 2);
    int mw = buf_w / 3;
    int mh = open ? (40 * r) / 100 : (12 * r) / 100;
    if (mh < 1) mh = 1;
    int corner = mh / 2;
    gfx_fill_rounded_rect(buf, buf_w, buf_rows, buf_w / 2 - mw / 2, cy - mh / 2, mw, mh, corner, fg);
}

// Builds up to 3 "Z" glyphs (a diagonal staircase, smallest/topmost last)
// over a repeating cycle, then resets to zero and builds again — a cheap,
// discrete stand-in for a drifting/fading sleep animation (no alpha
// blending available on this buffer).
#define ROBOT_ZZZ_STEP_MS 500u
#define ROBOT_ZZZ_STEPS   4u   // 0,1,2,3 glyphs, then wraps back to 0

// Below this eyes-band height, the full 8x8 'Z' looms disproportionately
// large relative to the tiny sleepy eye next to it (e.g. a 128x64 SSD1306's
// ~52px eyes band) — draw a downscaled 5x7 glyph instead.
#define ROBOT_ZZZ_COMPACT_PANEL_H 100
#define ROBOT_ZZZ_COMPACT_GLYPH_W 5
#define ROBOT_ZZZ_COMPACT_GLYPH_H 7

static void render_zzz(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                        int y_offset, uint32_t now_ms, uint16_t fg, uint16_t bg) {
    gfx_fill_rect(buf, buf_w, buf_rows, 0, 0, buf_w, buf_rows, bg);
    int band_top, band_h;
    robot_eyes_decor_band(panel_h, ROBOT_DECOR_ZZZ, &band_top, &band_h);
    (void)band_h;
    int local_bottom = band_top + band_h - y_offset;   // glyphs stack upward from here

    bool compact = panel_h < ROBOT_ZZZ_COMPACT_PANEL_H;
    int gw = compact ? ROBOT_ZZZ_COMPACT_GLYPH_W : DISPLAY_FONT_GLYPH_WIDTH;
    int gh = compact ? ROBOT_ZZZ_COMPACT_GLYPH_H : DISPLAY_FONT_GLYPH_HEIGHT;

    unsigned visible = (unsigned)((now_ms / ROBOT_ZZZ_STEP_MS) % ROBOT_ZZZ_STEPS);
    int base_x = (buf_w * 3) / 5;
    const uint8_t *glyph = display_font_glyph('Z');
    if (!glyph) return;
    uint8_t small[8];
    if (compact) {
        display_font_downscale(glyph, gw, gh, small);
        glyph = small;
    }
    for (unsigned i = 0; i < visible && i < 3; i++) {
        int gx = base_x + (int)i * (gw + 2);
        int gy = local_bottom - gh - (int)i * (gh - 2);
        for (int row = 0; row < gh; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < gw; col++) {
                if ((bits >> col) & 1) {
                    int px = gx + col, py = gy + row;
                    if (px >= 0 && px < buf_w && py >= 0 && py < buf_rows)
                        buf[py * buf_w + px] = fg;
                }
            }
        }
    }
}

// 5 vertical bars, each following its own phase-shifted triangle wave, so
// they ripple left-to-right like a simple audio-listening equalizer —
// deterministic (a pure function of now_ms), no randomness needed.
#define ROBOT_WAVES_BAR_COUNT 5u
#define ROBOT_WAVES_CYCLE_MS  800u

static void render_waves(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                          int y_offset, uint32_t now_ms, uint16_t fg, uint16_t bg) {
    gfx_fill_rect(buf, buf_w, buf_rows, 0, 0, buf_w, buf_rows, bg);
    int band_top, band_h;
    robot_eyes_decor_band(panel_h, ROBOT_DECOR_WAVES, &band_top, &band_h);
    int local_bottom = band_top + band_h - y_offset;   // bars grow upward from here

    int bar_w = buf_w / (int)(ROBOT_WAVES_BAR_COUNT * 2);
    if (bar_w < 1) bar_w = 1;
    int total_w = bar_w * (int)(2 * ROBOT_WAVES_BAR_COUNT - 1);
    int start_x = buf_w / 2 - total_w / 2;
    int min_h = band_h / 6;
    if (min_h < 1) min_h = 1;

    for (unsigned i = 0; i < ROBOT_WAVES_BAR_COUNT; i++) {
        uint32_t phase = (now_ms + i * (ROBOT_WAVES_CYCLE_MS / ROBOT_WAVES_BAR_COUNT)) % ROBOT_WAVES_CYCLE_MS;
        uint32_t half = ROBOT_WAVES_CYCLE_MS / 2;
        uint32_t tri = (phase < half) ? phase : (ROBOT_WAVES_CYCLE_MS - phase);   // 0..half, triangle wave
        int bh = min_h + (int)((uint32_t)(band_h - min_h) * tri / half);
        if (bh < 1) bh = 1;
        int bx = start_x + (int)i * 2 * bar_w;
        gfx_fill_rect(buf, buf_w, buf_rows, bx, local_bottom - bh, bar_w, bh, fg);
    }
}

void robot_eyes_render_decor(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                              int y_offset, uint32_t now_ms, robot_decor_t decor,
                              uint16_t fg_color, uint16_t bg_color) {
    switch (decor) {
    case ROBOT_DECOR_MOUTH:
        render_mouth(buf, buf_w, buf_rows, panel_h, y_offset, now_ms, fg_color, bg_color);
        break;
    case ROBOT_DECOR_ZZZ:
        render_zzz(buf, buf_w, buf_rows, panel_h, y_offset, now_ms, fg_color, bg_color);
        break;
    case ROBOT_DECOR_WAVES:
        render_waves(buf, buf_w, buf_rows, panel_h, y_offset, now_ms, fg_color, bg_color);
        break;
    default:
        gfx_fill_rect(buf, buf_w, buf_rows, 0, 0, buf_w, buf_rows, bg_color);
        break;
    }
}
