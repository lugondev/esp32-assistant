#pragma once
#include <stdint.h>
#include <stddef.h>

#define DISPLAY_FONT_GLYPH_WIDTH  8
#define DISPLAY_FONT_GLYPH_HEIGHT 8
#define DISPLAY_FONT_FIRST_CHAR   0x20
#define DISPLAY_FONT_LAST_CHAR    0x7E

// Returns a pointer to the 8-byte bitmap for `c` (one byte per pixel row,
// top row first; within a byte, bit 0 is the leftmost pixel), or NULL if
// `c` is outside [DISPLAY_FONT_FIRST_CHAR, DISPLAY_FONT_LAST_CHAR].
const uint8_t *display_font_glyph(char c);

// Computes the x pixel offset to horizontally center `text` (rendered at
// DISPLAY_FONT_GLYPH_WIDTH px per character, monospace) within a screen of
// width `screen_width` pixels. Returns the offset (>= 0), or -1 if `text`
// is wider than `screen_width`.
int display_layout_line(const char *text, int screen_width);

// Downscales one 8x8 glyph bitmap (display_font_glyph's format) to a
// smaller dst_w x dst_h bitmap, same row-per-byte/bit0=leftmost convention.
// Box-samples with OR (any source pixel lit in a destination cell lights
// that cell) rather than majority-vote, so thin single-pixel strokes
// survive the downscale instead of vanishing — this exists so small panels
// (e.g. a 128x64 SSD1306) can render legible-ish compact text without a
// second hand-authored glyph table; dst_w/dst_h must not exceed 8.
void display_font_downscale(const uint8_t src[8], int dst_w, int dst_h, uint8_t *dst);
