#include "robot_eyes.h"
#include "gfx.h"
#include "display_font.h"

// Static configuration (see robot_eyes.h): halo size, clamped so the
// dirty-band budget below always holds.
static int s_glow_pct = ROBOT_EYES_GLOW_PCT_DEFAULT;

void robot_eyes_set_glow_pct(int pct) {
    if (pct < 0) pct = 0;
    if (pct > ROBOT_EYES_GLOW_PCT_MAX) pct = ROBOT_EYES_GLOW_PCT_MAX;
    s_glow_pct = pct;
}

int robot_eyes_glow_pct(void) { return s_glow_pct; }

// Worst-case vertical reach across every emotion in EMOTIONS below, not
// just NEUTRAL — SURPRISED is the driver: height_pct=130 (half-height
// 1.3*0.8r=1.04r) plus |y_shift_pct|=15 (0.15r) plus the max glow pad
// (ROBOT_EYES_GLOW_PCT_MAX = 20 → 0.2r) = 1.39r. 140 gives a small safety margin. Recompute this if EMOTIONS ever
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
//   bottom_cut_pct: % of the eye's height erased from its bottom (glow
//     included), turning the squircle into an open-bottomed arc — the
//     happy/laughing/glee "smiling eye". 0 = full squircle.
typedef struct {
    int height_pct, width_pct, corner_pct, y_shift_pct, brow_slant_pct;
    int bottom_cut_pct;
} eye_params_t;

typedef enum { ROBOT_MOTION_NONE, ROBOT_MOTION_SHAKE, ROBOT_MOTION_BOUNCE,
               ROBOT_MOTION_OSCILLATE } robot_motion_t;
typedef enum { ROBOT_DROP_NONE, ROBOT_DROP_SWEAT, ROBOT_DROP_TEAR } robot_drop_t;

typedef struct {
    eye_params_t left, right;
    uint32_t blink_interval_ms, blink_duration_ms;
    robot_motion_t motion;
    int motion_amp_pct;   // SHAKE/BOUNCE: % of r; OSCILLATE: ± on height_pct
    robot_drop_t drop;
} emotion_params_t;

// One entry per robot_emotion_t value, same order as the enum. Values are
// this project's own creative choices, not copied from any reference —
// tuned by eye (pun intended) for a readable expression at ~54px eye
// height on a 240px-wide panel; adjust freely, but every entry must keep
//   0.8*height_pct(+oscillate amp) + |y_shift_pct| + bounce amp + 20 (glow)
//   <= ROBOT_EYES_MAX_REACH_PCT
// or the dirty-band canary test fails (and the screen would clip).
// KEEP IN SYNC with tools/emotions-preview.html EMOTIONS table.
static const emotion_params_t EMOTIONS[ROBOT_EMOTION_COUNT] = {
    //                             {  H,  W,  C,  Y,  B,Cut}
    [ROBOT_EMOTION_NEUTRAL]     = { {100,100,100,  0,  0, 0}, {100,100,100,  0,  0, 0},
                                    ROBOT_EYES_BLINK_INTERVAL_MS, ROBOT_EYES_BLINK_DURATION_MS,
                                    ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_HAPPY]       = { { 75,105,140, -5,  0,45}, { 75,105,140, -5,  0,45},
                                    3000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SAD]         = { { 85, 90, 70, 20,-40, 0}, { 85, 90, 70, 20,-40, 0},
                                    3500, 200, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SURPRISED]   = { {130,105,100,-15,  0, 0}, {130,105,100,-15,  0, 0},
                                    3000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_ANGRY]       = { { 55, 95, 25,  0, 45, 0}, { 55, 95, 25,  0, 45, 0},
                                    3000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SLEEPY]      = { { 30, 90, 50, 25,  0, 0}, { 30, 90, 50, 25,  0, 0},
                                    3000, 300, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    // Confused: one eyebrow raised, the other neutral — asym, plus a light
    // head-shake per the reference sheet's "confused · shake" tag.
    [ROBOT_EMOTION_CONFUSED]    = { {100,100,100,-15,-30, 0}, {100,100,100,  0,  0, 0},
                                    3000, 150, ROBOT_MOTION_SHAKE, 5, ROBOT_DROP_NONE },
    // Suspicious went asymmetric in the 28-state expansion (one narrowed,
    // one flatter eye) to match the reference sheet's "suspicious · asym".
    [ROBOT_EMOTION_SUSPICIOUS]  = { { 50,100, 35,  6, 25, 0}, { 30,100, 45, 10,  0, 0},
                                    4000, 200, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_LISTENING]   = { {105,100,100,  0,  0, 0}, {105,100,100,  0,  0, 0},
                                    5000, 120, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_PONDERING]   = { { 80, 95,100,  0,  0, 0}, { 80, 95,100,  0,  0, 0},
                                    4000, 250, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_FOCUSED]     = { { 45,100, 60,  0,  0, 0}, { 45,100, 60,  0,  0, 0},
                                    8000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_LAUGHING]    = { { 60,105,150, -8,  0,55}, { 60,105,150, -8,  0,55},
                                    3000, 150, ROBOT_MOTION_OSCILLATE, 15, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_GLEE]        = { { 50,105,160,-10,  0,60}, { 50,105,160,-10,  0,60},
                                    3000, 150, ROBOT_MOTION_BOUNCE, 8, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_AWE]         = { {130,100,200, -5,  0, 0}, {130,100,200, -5,  0, 0},
                                    1600, 100, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_CRYING]      = { { 80, 88, 70, 24,-45, 0}, { 80, 88, 70, 24,-45, 0},
                                    4500, 250, ROBOT_MOTION_NONE, 0, ROBOT_DROP_TEAR },
    [ROBOT_EMOTION_FURIOUS]     = { { 45, 95, 20,  4, 60, 0}, { 45, 95, 20,  4, 60, 0},
                                    2500, 120, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_FRUSTRATED]  = { { 55, 95, 25,  2, 40, 0}, { 55, 95, 25,  2, 40, 0},
                                    3000, 150, ROBOT_MOTION_SHAKE, 6, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_ANNOYED]     = { { 45, 95, 30,  6, 25, 0}, { 60, 95, 30,  0, 15, 0},
                                    3800, 200, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_UNIMPRESSED] = { { 30,100, 40, 10,  0, 0}, { 40,100, 40,  2,  0, 0},
                                    4500, 250, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_WORRIED]     = { { 75, 95, 60,  6,-35, 0}, { 75, 95, 60,  6,-35, 0},
                                    3000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_NERVOUS]     = { { 70, 95, 55,  6,-35, 0}, { 70, 95, 55,  6,-35, 0},
                                    2200, 120, ROBOT_MOTION_NONE, 0, ROBOT_DROP_SWEAT },
    [ROBOT_EMOTION_ANXIOUS]     = { { 80, 95, 55,  4,-40, 0}, { 80, 95, 55,  4,-40, 0},
                                    1400, 100, ROBOT_MOTION_NONE, 0, ROBOT_DROP_SWEAT },
    [ROBOT_EMOTION_SCARED]      = { {130,105, 80, -8,  0, 0}, {130,105, 80, -8,  0, 0},
                                    1800, 100, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SHOCKED]     = { {135,108,110,-10,  0, 0}, {135,108,110,-10,  0, 0},
                                    6000, 100, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_TIRED]       = { { 35, 92, 45, 18,-20, 0}, { 35, 92, 45, 18,-20, 0},
                                    3500, 300, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_BORED]       = { { 25,100, 40, 12,  0, 0}, { 25,100, 40, 12,  0, 0},
                                    7000, 400, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SKEPTICAL]   = { {100,100,100, -5,  0, 0}, { 35,100, 60,  8,  0, 0},
                                    4000, 200, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SQUINT]      = { { 18,100, 30,  0,  0, 0}, { 18,100, 30,  0,  0, 0},
                                    9000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
};

// Same order as the enum — a new emotion without a name here renders as
// "?" and fails test_emotion_names_cover_all_states.
static const char *const EMOTION_NAMES[ROBOT_EMOTION_COUNT] = {
    [ROBOT_EMOTION_NEUTRAL]     = "neutral",
    [ROBOT_EMOTION_HAPPY]       = "happy",
    [ROBOT_EMOTION_SAD]         = "sad",
    [ROBOT_EMOTION_SURPRISED]   = "surprised",
    [ROBOT_EMOTION_ANGRY]       = "angry",
    [ROBOT_EMOTION_SLEEPY]      = "sleepy",
    [ROBOT_EMOTION_CONFUSED]    = "confused",
    [ROBOT_EMOTION_SUSPICIOUS]  = "suspicious",
    [ROBOT_EMOTION_LISTENING]   = "listening",
    [ROBOT_EMOTION_PONDERING]   = "pondering",
    [ROBOT_EMOTION_FOCUSED]     = "focused",
    [ROBOT_EMOTION_LAUGHING]    = "laughing",
    [ROBOT_EMOTION_GLEE]        = "glee",
    [ROBOT_EMOTION_AWE]         = "awe",
    [ROBOT_EMOTION_CRYING]      = "crying",
    [ROBOT_EMOTION_FURIOUS]     = "furious",
    [ROBOT_EMOTION_FRUSTRATED]  = "frustrated",
    [ROBOT_EMOTION_ANNOYED]     = "annoyed",
    [ROBOT_EMOTION_UNIMPRESSED] = "unimpressed",
    [ROBOT_EMOTION_WORRIED]     = "worried",
    [ROBOT_EMOTION_NERVOUS]     = "nervous",
    [ROBOT_EMOTION_ANXIOUS]     = "anxious",
    [ROBOT_EMOTION_SCARED]      = "scared",
    [ROBOT_EMOTION_SHOCKED]     = "shocked",
    [ROBOT_EMOTION_TIRED]       = "tired",
    [ROBOT_EMOTION_BORED]       = "bored",
    [ROBOT_EMOTION_SKEPTICAL]   = "skeptical",
    [ROBOT_EMOTION_SQUINT]      = "squint",
};

const char *robot_eyes_emotion_name(robot_emotion_t emotion) {
    if ((unsigned)emotion >= ROBOT_EMOTION_COUNT) return "?";
    const char *n = EMOTION_NAMES[emotion];
    return n ? n : "?";
}

bool robot_eyes_is_closed_for(robot_emotion_t emotion, uint32_t now_ms) {
    if ((unsigned)emotion >= ROBOT_EMOTION_COUNT) emotion = ROBOT_EMOTION_NEUTRAL;
    const emotion_params_t *em = &EMOTIONS[emotion];
    return (now_ms % em->blink_interval_ms) < em->blink_duration_ms;
}

bool robot_eyes_is_closed(uint32_t now_ms) {
    return robot_eyes_is_closed_for(ROBOT_EMOTION_NEUTRAL, now_ms);
}

// Renders one eye (already-scaled box at ex,ey,ew,eh) plus its glow halo and
// optional brow-slant wedge. is_left selects which top corner is "outer"
// (away from the nose) vs "inner" (toward the nose) for the wedge.
static void render_eye_box(uint16_t *buf, int buf_w, int buf_rows,
                            int ex, int ey, int ew, int eh, int corner_r,
                            int glow_pad_x, int glow_pad_y, int brow_slant_pct,
                            int bottom_cut_pct, bool is_left, uint16_t eye_color,
                            uint16_t glow_color, uint16_t bg_color) {
    int gw = ew + 2 * glow_pad_x, gh = eh + 2 * glow_pad_y;
    int gr = corner_r + glow_pad_y;
    int gx = ex - glow_pad_x, gy = ey - glow_pad_y;
    // Glow pct 0 zeroes both pads — skip the halo entirely rather than
    // drawing a dim rect the bright eye would fully overpaint anyway.
    if (glow_pad_x > 0 || glow_pad_y > 0)
        gfx_fill_rounded_rect(buf, buf_w, buf_rows, gx, gy, gw, gh, gr, glow_color);
    gfx_fill_rounded_rect(buf, buf_w, buf_rows, ex, ey, ew, eh, corner_r, eye_color);

    if (bottom_cut_pct > 0) {
        int cut_h = (eh * bottom_cut_pct) / 100;
        if (cut_h > 0) {
            // Erase the eye's bottom AND the glow beneath it so the open
            // arc reads cleanly against the background.
            int cut_y = ey + eh - cut_h;
            gfx_fill_rect(buf, buf_w, buf_rows, gx, cut_y, gw, (gy + gh) - cut_y, bg_color);
        }
    }

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

// Water drop: a filled circle with a pointed triangular tail above it.
// SWEAT pulses on/off on an 800ms cycle at the right eye's top-outer
// corner; TEAR slides from just under the right eye down to the dirty
// band's bottom edge on a 1200ms sawtooth, then resets. Drawn inside the
// eyes band — no new decor band, no extra SPI cost.
#define ROBOT_SWEAT_CYCLE_MS   800u
#define ROBOT_SWEAT_VISIBLE_MS 500u
#define ROBOT_TEAR_CYCLE_MS   1200u

static void render_drop(uint16_t *buf, int buf_w, int buf_rows, robot_drop_t drop,
                        int ex, int ey, int ew, int eh, int r, int band_bottom_y,
                        uint32_t now_ms, uint16_t color) {
    int dr = r / 8;
    if (dr < 1) dr = 1;
    int cx, cy;
    if (drop == ROBOT_DROP_SWEAT) {
        if ((now_ms % ROBOT_SWEAT_CYCLE_MS) >= ROBOT_SWEAT_VISIBLE_MS) return;
        cx = ex + ew;
        cy = ey - dr;
    } else if (drop == ROBOT_DROP_TEAR) {
        int start_y = ey + eh + 2 * dr;
        int end_y = band_bottom_y - dr - 1;
        if (end_y < start_y) end_y = start_y;
        uint32_t ph = now_ms % ROBOT_TEAR_CYCLE_MS;
        cy = start_y + (int)(((uint32_t)(end_y - start_y) * ph) / ROBOT_TEAR_CYCLE_MS);
        cx = ex + (3 * ew) / 4;
    } else {
        return;
    }
    gfx_fill_circle(buf, buf_w, buf_rows, cx, cy, dr, color);
    gfx_fill_triangle(buf, buf_w, buf_rows, cx, cy - 3 * dr,
                       cx - dr, cy, cx + dr, cy, color);
}

// Defined with the other "Z Z Z" pieces further down; robot_eyes_render
// calls it for SLEEPY to draw the cluster in-band, next to the right eye.
static void render_zzz_cluster(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                                int bottom_y, int base_x, uint32_t now_ms, uint16_t fg);

// Motion is a pure function of now_ms (square/triangle waves) — no RNG,
// preserving this file's deterministic contract. dx/dy are pixel offsets;
// dh_pct adds to height_pct before scaling.
static void motion_offsets(const emotion_params_t *em, int r, uint32_t now_ms,
                           int *dx, int *dy, int *dh_pct) {
    *dx = 0; *dy = 0; *dh_pct = 0;
    int amp = em->motion_amp_pct;
    switch (em->motion) {
    case ROBOT_MOTION_SHAKE: {          // horizontal jitter, 120ms square wave
        int a = (r * amp) / 100;
        if (a < 1) a = 1;
        *dx = ((now_ms / 60u) % 2u) ? a : -a;
        break;
    }
    case ROBOT_MOTION_BOUNCE: {         // vertical hop 0→-amp→0, 600ms triangle
        uint32_t ph = now_ms % 600u;
        uint32_t tri = ph < 300u ? ph : 600u - ph;
        *dy = -(int)(((uint32_t)((r * amp) / 100) * tri) / 300u);
        break;
    }
    case ROBOT_MOTION_OSCILLATE: {      // height_pct wobble ±amp, 250ms triangle
        uint32_t ph = now_ms % 250u;
        uint32_t tri = ph < 125u ? ph : 250u - ph;
        *dh_pct = (int)((2u * (uint32_t)amp * tri) / 125u) - amp;
        break;
    }
    default: break;
    }
}

void robot_eyes_render(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                        int y_offset, uint32_t now_ms, robot_emotion_t emotion,
                        uint16_t eye_color, uint16_t bg_color) {
    if ((unsigned)emotion >= ROBOT_EMOTION_COUNT) emotion = ROBOT_EMOTION_NEUTRAL;
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
    // A non-zero glow setting must stay visible even on tiny panels where
    // the percentage rounds to 0 px (e.g. r=5 on a 128x64 test canvas) —
    // clamp both pads to at least 1 px. Only pct == 0 disables the halo.
    int glow_pad_x  = (max_half_w * s_glow_pct) / 100;
    if (s_glow_pct > 0 && glow_pad_x < 1) glow_pad_x = 1;
    int base_eye_half_w = max_half_w - glow_pad_x;
    int base_eye_w = 2 * base_eye_half_w;

    int base_eye_h = (8 * r) / 5;   // 1.6r
    // Vertical glow pad is configured as a % of r, clamped to
    // ROBOT_EYES_GLOW_PCT_MAX (0.2r) — exactly the glow headroom the
    // dirty-band budget (ROBOT_EYES_MAX_REACH_PCT) reserves, so any
    // allowed setting keeps every emotion inside the band.
    int glow_pad_y = (r * s_glow_pct) / 100;
    if (s_glow_pct > 0 && glow_pad_y < 1) glow_pad_y = 1;
    uint16_t glow_color = dim_rgb565(eye_color);

    const emotion_params_t *em = &EMOTIONS[emotion];
    const eye_params_t *sides[2] = { &em->left, &em->right };
    int cxs[2] = { left_cx, right_cx };
    bool is_left[2] = { true, false };

    int mdx, mdy, mdh;
    motion_offsets(em, r, now_ms, &mdx, &mdy, &mdh);

    bool closed = robot_eyes_is_closed_for(emotion, now_ms);
    int right_ex = 0, right_ey = 0, right_ew = 0, right_eh = 0;
    for (int i = 0; i < 2; i++) {
        const eye_params_t *p = sides[i];
        int ew = (base_eye_w * p->width_pct) / 100;
        int eh = (base_eye_h * (p->height_pct + mdh)) / 100;
        int corner_r = (ew * 2) / 10;   // 0.2 * ew, then further scaled below
        corner_r = (corner_r * p->corner_pct) / 100;
        int cy = cy0 + (r * p->y_shift_pct) / 100 + mdy;
        int ex = cxs[i] + mdx - ew / 2, ey = cy - eh / 2;
        if (i == 1) { right_ex = ex; right_ey = ey; right_ew = ew; right_eh = eh; }

        if (!closed) {
            render_eye_box(buf, buf_w, buf_rows, ex, ey, ew, eh, corner_r,
                            glow_pad_x, glow_pad_y, p->brow_slant_pct,
                            p->bottom_cut_pct, is_left[i], eye_color, glow_color,
                            bg_color);
        } else {
            int band_h = eh / 5;
            if (band_h < 1) band_h = 1;
            gfx_fill_rect(buf, buf_w, buf_rows, ex, cy - band_h / 2, ew, band_h, eye_color);
        }
    }

    // Drops anchor on the right eye's OPEN geometry (computed above whether
    // or not this frame is mid-blink) so they don't jump during a blink.
    if (em->drop != ROBOT_DROP_NONE) {
        int band_bottom = cy0 + (r * ROBOT_EYES_MAX_REACH_PCT) / 100;
        render_drop(buf, buf_w, buf_rows, em->drop, right_ex, right_ey,
                     right_ew, right_eh, r, band_bottom, now_ms, eye_color);
    }

    // SLEEPY's "Z Z Z" hugs the right eye the same way the drops do —
    // anchored a small gap above its top edge, staircase climbing up-right.
    // Drawn whether the eye is open or closed (sleeping eyes are mostly
    // closed; the Zs are what sells the state).
    if (emotion == ROBOT_EMOTION_SLEEPY) {
        int gap = r / 10;
        if (gap < 1) gap = 1;
        render_zzz_cluster(buf, buf_w, buf_rows, panel_h,
                            right_ey - gap, right_ex + (2 * right_ew) / 5,
                            now_ms, eye_color);
    }
}

robot_decor_t robot_eyes_decor_for(robot_emotion_t emotion) {
    switch (emotion) {
    case ROBOT_EMOTION_HAPPY:     return ROBOT_DECOR_MOUTH;
    case ROBOT_EMOTION_LAUGHING:  return ROBOT_DECOR_MOUTH;
    // SURPRISED keeps WAVES for compatibility: main.c still uses it as its
    // "listening" state. Once the FSM adopts ROBOT_EMOTION_LISTENING,
    // consider dropping WAVES from SURPRISED.
    case ROBOT_EMOTION_SURPRISED: return ROBOT_DECOR_WAVES;
    case ROBOT_EMOTION_LISTENING: return ROBOT_DECOR_WAVES;
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
#define ROBOT_MOUTH_TOP_PCT    145
#define ROBOT_MOUTH_HEIGHT_PCT  50
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
// blending available on this buffer). Drawn in-band by robot_eyes_render,
// anchored just above the right eye's top edge — its previous life as a
// decor band pinned it to the dirty band's top, a half-panel away from the
// droopy sleepy eyes.
#define ROBOT_ZZZ_STEP_MS 500u
#define ROBOT_ZZZ_STEPS   4u   // 0,1,2,3 glyphs, then wraps back to 0

// Below this eyes-band height, the full 8x8 'Z' looms disproportionately
// large relative to the tiny sleepy eye next to it (e.g. a 128x64 SSD1306's
// ~52px eyes band) — draw a downscaled 5x7 glyph instead.
#define ROBOT_ZZZ_COMPACT_PANEL_H 100
#define ROBOT_ZZZ_COMPACT_GLYPH_W 5
#define ROBOT_ZZZ_COMPACT_GLYPH_H 7

// bottom_y: the cluster's bottommost row (the first glyph's baseline);
// base_x: the first (largest, lowest) glyph's left edge. The staircase
// climbs up-right from there.
static void render_zzz_cluster(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                                int bottom_y, int base_x, uint32_t now_ms, uint16_t fg) {
    bool compact = panel_h < ROBOT_ZZZ_COMPACT_PANEL_H;
    int gw = compact ? ROBOT_ZZZ_COMPACT_GLYPH_W : DISPLAY_FONT_GLYPH_WIDTH;
    int gh = compact ? ROBOT_ZZZ_COMPACT_GLYPH_H : DISPLAY_FONT_GLYPH_HEIGHT;

    unsigned visible = (unsigned)((now_ms / ROBOT_ZZZ_STEP_MS) % ROBOT_ZZZ_STEPS);
    const uint8_t *glyph = display_font_glyph('Z');
    if (!glyph) return;
    uint8_t small[8];
    if (compact) {
        display_font_downscale(glyph, gw, gh, small);
        glyph = small;
    }
    for (unsigned i = 0; i < visible && i < 3; i++) {
        int gx = base_x + (int)i * (gw + 2);
        int gy = bottom_y - gh - (int)i * (gh - 2);
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
    case ROBOT_DECOR_WAVES:
        render_waves(buf, buf_w, buf_rows, panel_h, y_offset, now_ms, fg_color, bg_color);
        break;
    default:
        gfx_fill_rect(buf, buf_w, buf_rows, 0, 0, buf_w, buf_rows, bg_color);
        break;
    }
}
