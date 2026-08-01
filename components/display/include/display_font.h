#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

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

// Vertical gap between the two lines of display_show()'s text screen.
#define DISPLAY_FONT_LINE_GAP 4

// Vertical placement for display_show()'s one- or two-line text screen: a
// single line is centered on its own, a pair is centered as one block with
// DISPLAY_FONT_LINE_GAP between them. *y2 is only meaningful when two_lines.
//
// Shared because both panel drivers render that screen and each used to carry
// its own copy of this arithmetic, gap constant included.
void display_layout_lines(int screen_height, bool two_lines, int *y1, int *y2);

// Called once per character by display_font_draw_centered, with the glyph
// bitmap (display_font_glyph's format) and its top-left position.
typedef void (*display_font_glyph_fn)(int x, int y, const uint8_t *glyph,
                                       void *ctx);

// Walks `text` left to right at the centering offset display_layout_line
// computes, invoking `put` per character. Draws nothing at all if the text is
// wider than screen_width (the same skip-rather-than-wrap rule), and skips
// characters outside the font's range.
//
// A callback rather than a buffer because the two panel drivers put a glyph on
// screen in genuinely different ways — bit-poking a page-packed shadow
// framebuffer on the SSD1306, one esp_lcd bitmap transaction per glyph on the
// ST7789 — while the walk itself was identical in both.
void display_font_draw_centered(const char *text, int y, int screen_width,
                                 display_font_glyph_fn put, void *ctx);

// Downscales one 8x8 glyph bitmap (display_font_glyph's format) to a
// smaller dst_w x dst_h bitmap, same row-per-byte/bit0=leftmost convention.
// Box-samples with OR (any source pixel lit in a destination cell lights
// that cell) rather than majority-vote, so thin single-pixel strokes
// survive the downscale instead of vanishing — this exists so small panels
// (e.g. a 128x64 SSD1306) can render legible-ish compact text without a
// second hand-authored glyph table; dst_w/dst_h must not exceed 8.
void display_font_downscale(const uint8_t src[8], int dst_w, int dst_h, uint8_t *dst);
