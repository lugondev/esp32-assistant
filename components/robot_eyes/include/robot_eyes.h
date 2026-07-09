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

// Renders two eyes into buf (buf_w * buf_h RGB565 pixels), first filling
// the whole buffer with bg_color. Geometry contract (fixed, part of the
// public behavior):
//   eye_radius = buf_h / 4
//   left eye center  = (buf_w/4,   buf_h/2)
//   right eye center = (3*buf_w/4, buf_h/2)
// Open eyes are filled circles of eye_radius. Closed eyes are a
// horizontal band eye_radius/3 tall (clamped to >=1px) through the same
// centers, so a fully-open pixel directly above/below center reverts to
// bg_color while closed.
void robot_eyes_render(uint16_t *buf, int buf_w, int buf_h, uint32_t now_ms,
                        uint16_t eye_color, uint16_t bg_color);
