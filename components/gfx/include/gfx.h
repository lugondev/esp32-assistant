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
