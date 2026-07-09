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

// Renders two eyes into buf, a buf_w-wide, buf_rows-tall RGB565 crop of a
// buf_w x panel_h panel, where buf row 0 corresponds to panel row y_offset
// (use robot_eyes_dirty_band(panel_h, ...) to size/position this crop so
// it covers exactly the rows that change). Geometry is computed against
// the full panel_h so eyes land in the same place regardless of how much
// is cropped; rows outside [y_offset, y_offset+buf_rows) are simply never
// painted (same silent-clip semantics gfx already has). Pass buf_rows ==
// panel_h and y_offset == 0 to render the whole panel in one buffer, same
// as before this crop support was added.
// Fixed geometry contract (part of the public behavior):
//   eye_radius = panel_h / 4
//   left eye center  = (buf_w/4,   panel_h/2)
//   right eye center = (3*buf_w/4, panel_h/2)
// Open eyes are filled circles of eye_radius. Closed eyes are a
// horizontal band eye_radius/3 tall (clamped to >=1px) through the same
// centers, so a fully-open pixel directly above/below center reverts to
// bg_color while closed.
void robot_eyes_render(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                        int y_offset, uint32_t now_ms,
                        uint16_t eye_color, uint16_t bg_color);
