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

// Paints a 1-bit-per-pixel bitmap at (x,y): `rows[i]` is row i, and within a
// byte bit 0 is the LEFTMOST pixel (display_font_glyph's layout, and what
// display_font_downscale produces). Only w columns of each of the h rows are
// read, so a downscaled 5x7 glyph can be passed in an 8-byte array.
//
// Lit bits are painted in `color`; clear bits are transparent — they leave
// whatever the caller already drew underneath. That is what makes this usable
// for text over a rendered background, and it is why the callers cannot just
// use gfx_fill_rect per pixel.
//
// Font-agnostic on purpose: the glyph table lives in components/display, and
// having gfx reach for it would invert the dependency. Callers fetch (and
// optionally downscale) the bitmap, then hand it here — which is exactly the
// nested bit-loop that statusbar and robot_eyes each had their own copy of.
void gfx_blit_bitmap1(uint16_t *buf, int buf_w, int buf_h, int x, int y,
                       const uint8_t *rows, int w, int h, uint16_t color);
