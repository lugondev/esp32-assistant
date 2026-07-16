#pragma once
#include <stdint.h>
#include <stdbool.h>

// Stateless "robot eyes" blink schedule + renderer. No hardware access —
// host-testable. Time is the only per-frame input: given the same now_ms
// (and the same static configuration below), behavior is always identical
// (no RNG, no animation state carried between calls).

// NEUTRAL's blink schedule. Every emotion has its own interval/duration in
// robot_eyes.c's EMOTIONS table (a bored eye blinks slowly, an anxious one
// fast); these two constants remain as NEUTRAL's entry and as the legacy
// default robot_eyes_is_closed() answers for.
#define ROBOT_EYES_BLINK_INTERVAL_MS 3000u  // time between blinks
#define ROBOT_EYES_BLINK_DURATION_MS 150u   // how long a blink stays closed

// True if, at now_ms, the eyes should render closed. Equivalent to
// robot_eyes_is_closed_for(ROBOT_EMOTION_NEUTRAL, now_ms).
bool robot_eyes_is_closed(uint32_t now_ms);

// The panel-space vertical band [*y, *y + *height) that ever changes across
// frames for a panel of height panel_h — both eyes live in this band, and
// everything above/below it is always pure bg_color. A caller redrawing
// every frame only needs a buffer/flush covering this band, not the full
// panel_h rows, since the rest never changes between frames.
void robot_eyes_dirty_band(int panel_h, int *y, int *height);

// Emotions are mutually exclusive parametric presets (height/width/corner
// scale, vertical shift, eyebrow slant, bottom cut) applied on top of the
// same base squircle+glow shape — not separate bitmaps — plus per-emotion
// blink schedule, an optional motion (shake/bounce/oscillate) and an
// optional sweat/tear drop. Most are mirrored across both eyes; the "asym"
// ones (CONFUSED, SKEPTICAL, SUSPICIOUS, ANNOYED, UNIMPRESSED) deliberately
// aren't, which is why robot_eyes_render takes a single emotion and
// resolves left/right parameters internally rather than exposing per-eye
// knobs to the caller.
//
// 28 states in 7 groups. The first 8 values predate the expansion — their
// names and order are frozen (main.c and tests reference them); new states
// are only ever appended before ROBOT_EMOTION_COUNT.
typedef enum {
    // original 8 — order frozen
    ROBOT_EMOTION_NEUTRAL,
    ROBOT_EMOTION_HAPPY,
    ROBOT_EMOTION_SAD,
    ROBOT_EMOTION_SURPRISED,
    ROBOT_EMOTION_ANGRY,
    ROBOT_EMOTION_SLEEPY,
    ROBOT_EMOTION_CONFUSED,
    ROBOT_EMOTION_SUSPICIOUS,
    // group 1 — neutral & attention
    ROBOT_EMOTION_LISTENING,
    ROBOT_EMOTION_PONDERING,
    ROBOT_EMOTION_FOCUSED,
    // group 2 — positive
    ROBOT_EMOTION_LAUGHING,
    ROBOT_EMOTION_GLEE,
    ROBOT_EMOTION_AWE,
    // group 3 — negative
    ROBOT_EMOTION_CRYING,
    // group 4 — angry / frustration
    ROBOT_EMOTION_FURIOUS,
    ROBOT_EMOTION_FRUSTRATED,
    ROBOT_EMOTION_ANNOYED,
    ROBOT_EMOTION_UNIMPRESSED,
    // group 5 — fear & anxiety
    ROBOT_EMOTION_WORRIED,
    ROBOT_EMOTION_NERVOUS,
    ROBOT_EMOTION_ANXIOUS,
    ROBOT_EMOTION_SCARED,
    ROBOT_EMOTION_SHOCKED,
    // group 6 — tired / low energy
    ROBOT_EMOTION_TIRED,
    ROBOT_EMOTION_BORED,
    // group 7 — suspicion / judgement
    ROBOT_EMOTION_SKEPTICAL,
    ROBOT_EMOTION_SQUINT,
    ROBOT_EMOTION_COUNT,
} robot_emotion_t;

// Per-emotion blink schedule (interval/duration from robot_eyes.c's
// EMOTIONS table). robot_eyes_is_closed(t) is this with NEUTRAL.
bool robot_eyes_is_closed_for(robot_emotion_t emotion, uint32_t now_ms);

// Short lowercase display name ("neutral", "glee", ...) — shown on the
// status bar when an emotion is picked manually (e.g. the random-emotion
// button). Out-of-range values return "?", never NULL.
const char *robot_eyes_emotion_name(robot_emotion_t emotion);

// Glow "border" halo drawn behind each eye, sized as a % of r (the eye
// radius unit, panel_h/4) vertically and of the eye's width budget
// horizontally. Static configuration, not animation state — render output
// stays a pure function of (config, emotion, now_ms). 0 disables the halo
// entirely (eyes also widen slightly, reclaiming the halo's width budget).
// Values outside [0, ROBOT_EYES_GLOW_PCT_MAX] are clamped: the dirty-band
// budget (ROBOT_EYES_MAX_REACH_PCT in robot_eyes.c) only reserves 0.2r of
// vertical headroom for the glow.
#define ROBOT_EYES_GLOW_PCT_DEFAULT 10
#define ROBOT_EYES_GLOW_PCT_MAX     20
void robot_eyes_set_glow_pct(int pct);
int  robot_eyes_glow_pct(void);

// Renders two eyes into buf, a buf_w-wide, buf_rows-tall RGB565 crop of a
// buf_w x panel_h panel, where buf row 0 corresponds to panel row y_offset
// (use robot_eyes_dirty_band(panel_h, ...) to size/position this crop so
// it covers exactly the rows that change). Geometry is computed against
// the full panel_h so eyes land in the same place regardless of how much
// is cropped; rows outside [y_offset, y_offset+buf_rows) are simply never
// painted (same silent-clip semantics gfx already has). Pass buf_rows ==
// panel_h and y_offset == 0 to render the whole panel in one buffer, same
// as before this crop support was added.
//
// Base geometry contract (ROBOT_EMOTION_NEUTRAL; other emotions scale these
// per-eye — see robot_eyes.c's emotion table):
//   r = panel_h / 4
//   left eye center  = (buf_w/4,   panel_h/2)
//   right eye center = (3*buf_w/4, panel_h/2)
// Eye width is capped by buf_w (not just r) so the two eyes' glow halos
// never overlap each other or the screen edges regardless of panel aspect
// ratio. Open eyes are rounded-rect "squircles" with a dimmer, larger
// rounded-rect glow halo behind them; some emotions also cut a triangular
// wedge from one top corner (a cheap "eyebrow slant") via gfx_fill_triangle.
// Closed (blinking) eyes are a plain horizontal band at the emotion's
// current width/position, with no glow or brow wedge.
void robot_eyes_render(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                        int y_offset, uint32_t now_ms, robot_emotion_t emotion,
                        uint16_t eye_color, uint16_t bg_color);

// Optional decorations, deliberately kept OUT of robot_eyes_dirty_band /
// robot_eyes_render: only a few emotions have one, and folding any of them
// into the always-rendered eye band would make every emotion pay the extra
// SPI bytes for a mouth/wave it never shows. A caller checks
// robot_eyes_decor_for(emotion) and only allocates/flushes the decor band
// when it's not ROBOT_DECOR_NONE. (SLEEPY's "Z Z Z" used to be a decor up
// here at the band's top edge, a half-panel away from the droopy sleepy
// eyes — it's now drawn by robot_eyes_render itself, in-band, hugging the
// right eye, the same way the sweat/tear drops are.)
typedef enum {
    ROBOT_DECOR_NONE,
    ROBOT_DECOR_MOUTH,   // HAPPY/LAUGHING — an animated "talking" mouth below the eyes
    ROBOT_DECOR_WAVES,   // SURPRISED/LISTENING — animated equalizer-style listening bars
} robot_decor_t;

robot_decor_t robot_eyes_decor_for(robot_emotion_t emotion);

// Panel-space band [*y, *y + *height) the given decor ever paints in, same
// crop convention as robot_eyes_dirty_band. Undefined for ROBOT_DECOR_NONE.
void robot_eyes_decor_band(int panel_h, robot_decor_t decor, int *y, int *height);

// Renders decor into buf (a buf_w-wide crop per robot_eyes_decor_band,
// y_offset panel rows down — same convention as robot_eyes_render).
// bg_color first fills the whole buffer, matching robot_eyes_render.
void robot_eyes_render_decor(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                              int y_offset, uint32_t now_ms, robot_decor_t decor,
                              uint16_t fg_color, uint16_t bg_color);
