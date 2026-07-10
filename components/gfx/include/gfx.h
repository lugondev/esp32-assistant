#pragma once
#include <stdint.h>

// Pure software drawing into a caller-owned RGB565 buffer of size
// buf_w * buf_h. No hardware access — these are host-testable primitives
// used by both the display flush path and animation renderers (robot_eyes).
// All shapes clip silently to the buffer bounds (no partial-pixel writes
// outside [0,buf_w)x[0,buf_h)).

void gfx_fill_rect(uint16_t *buf, int buf_w, int buf_h,
                    int x, int y, int w, int h, uint16_t color);

void gfx_fill_circle(uint16_t *buf, int buf_w, int buf_h,
                      int cx, int cy, int r, uint16_t color);

// Fills a w x h rectangle at (x,y) with corners rounded to radius r (clamped
// to at most min(w,h)/2). r=0 is identical to gfx_fill_rect.
void gfx_fill_rounded_rect(uint16_t *buf, int buf_w, int buf_h,
                            int x, int y, int w, int h, int r, uint16_t color);

// Fills the triangle with vertices (x0,y0),(x1,y1),(x2,y2). Vertex order
// doesn't matter (any winding). Used to mask/add angled wedges — e.g. an
// eyebrow-slant cut into an eye shape — that gfx's other primitives can't
// express on their own.
void gfx_fill_triangle(uint16_t *buf, int buf_w, int buf_h,
                        int x0, int y0, int x1, int y1, int x2, int y2,
                        uint16_t color);
