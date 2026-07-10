#pragma once
#include <stdint.h>
#include <stdbool.h>

// Stateless "robot eyes" blink schedule + renderer. No hardware access —
// host-testable. Time is the only input: given the same now_ms, behavior
// is always identical (no RNG, no mutable state to carry between calls).

#define ROBOT_EYES_BLINK_INTERVAL_MS 3000u  // time between blinks
#define ROBOT_EYES_BLINK_DURATION_MS 150u   // how long a blink stays closed

// True if, at now_ms, the eyes should render closed. Blinks recur every
// ROBOT_EYES_BLINK_INTERVAL_MS and last ROBOT_EYES_BLINK_DURATION_MS.
bool robot_eyes_is_closed(uint32_t now_ms);

// The panel-space vertical band [*y, *y + *height) that ever changes across
// frames for a panel of height panel_h — both eyes live in this band, and
// everything above/below it is always pure bg_color. A caller redrawing
// every frame only needs a buffer/flush covering this band, not the full
// panel_h rows, since the rest never changes between frames.
void robot_eyes_dirty_band(int panel_h, int *y, int *height);

// Emotions are mutually exclusive parametric presets (height/width/corner
// scale, vertical shift, eyebrow slant) applied on top of the same base
// squircle+glow shape — not separate bitmaps. Most are mirrored across both
// eyes; CONFUSED deliberately isn't (one eyebrow raised), which is why
// robot_eyes_render takes a single emotion and resolves left/right
// parameters internally rather than exposing per-eye knobs to the caller.
typedef enum {
    ROBOT_EMOTION_NEUTRAL,
    ROBOT_EMOTION_HAPPY,
    ROBOT_EMOTION_SAD,
    ROBOT_EMOTION_SURPRISED,
    ROBOT_EMOTION_ANGRY,
    ROBOT_EMOTION_SLEEPY,
    ROBOT_EMOTION_CONFUSED,
    ROBOT_EMOTION_SUSPICIOUS,
} robot_emotion_t;

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
// SPI bytes for a mouth/"Zzz"/wave it never shows. A caller checks
// robot_eyes_decor_for(emotion) and only allocates/flushes the decor band
// when it's not ROBOT_DECOR_NONE.
typedef enum {
    ROBOT_DECOR_NONE,
    ROBOT_DECOR_MOUTH,   // HAPPY — an animated "talking" mouth below the eyes
    ROBOT_DECOR_ZZZ,     // SLEEPY — a small building/resetting "Z Z Z" cluster
    ROBOT_DECOR_WAVES,   // SURPRISED — animated equalizer-style listening bars
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
