#include "display_font.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_glyph_space(void) {
    const uint8_t *g = display_font_glyph(' ');
    CHECK(g != NULL);
    for (int i = 0; i < 8; i++) CHECK(g[i] == 0x00);
}

static void test_glyph_known_letter(void) {
    const uint8_t *a = display_font_glyph('A');
    CHECK(a != NULL);
    CHECK(a[0] == 0x0C);
    CHECK(a[7] == 0x00);
}

static void test_glyph_first_and_last(void) {
    CHECK(display_font_glyph((char)0x20) != NULL);
    CHECK(display_font_glyph((char)0x7E) != NULL);
}

static void test_glyph_out_of_range(void) {
    CHECK(display_font_glyph((char)0x1F) == NULL);
    CHECK(display_font_glyph((char)0x7F) == NULL);
}

static void test_glyph_distinct(void) {
    const uint8_t *a = display_font_glyph('A');
    const uint8_t *b = display_font_glyph('B');
    CHECK(memcmp(a, b, 8) != 0);
}

static void test_layout_centers_short_text(void) {
    // "OK" is 2 chars * 8px = 16px wide; centered in 240px -> (240-16)/2 = 112
    CHECK(display_layout_line("OK", 240) == 112);
}

static void test_layout_centers_empty_string(void) {
    CHECK(display_layout_line("", 240) == 120);
}

static void test_layout_exact_fit(void) {
    // 30 chars * 8px = 240px, exactly fills a 240px screen -> offset 0
    CHECK(display_layout_line("123456789012345678901234567890", 240) == 0);
}

static void test_layout_too_wide(void) {
    // 31 chars * 8px = 248px > 240px screen
    CHECK(display_layout_line("1234567890123456789012345678901", 240) == -1);
}

static void test_downscale_fully_lit_glyph_stays_fully_lit(void) {
    static const uint8_t full[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t out[7];
    display_font_downscale(full, 5, 7, out);
    for (int i = 0; i < 7; i++) CHECK(out[i] == 0x1F);  // 5 lit bits
}

static void test_downscale_blank_glyph_stays_blank(void) {
    static const uint8_t blank[8] = {0};
    uint8_t out[7];
    display_font_downscale(blank, 5, 7, out);
    for (int i = 0; i < 7; i++) CHECK(out[i] == 0x00);
}

static void test_downscale_preserves_a_single_top_left_pixel(void) {
    // Only the top-left source pixel lit -> should still land in the
    // top-left region of the downscaled output (output row 0, some bit
    // among the leftmost columns), not vanish entirely.
    static const uint8_t corner[8] = {0x01,0,0,0,0,0,0,0};
    uint8_t out[7];
    display_font_downscale(corner, 5, 7, out);
    bool any_lit = false;
    for (int i = 0; i < 7; i++) if (out[i]) any_lit = true;
    CHECK(any_lit);
    CHECK((out[0] & 0x01) != 0);  // maps to output row 0, leftmost column
}

static void test_downscale_output_width_never_exceeds_requested(void) {
    static const uint8_t full[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t out[7];
    display_font_downscale(full, 5, 7, out);
    for (int i = 0; i < 7; i++) CHECK((out[i] & ~0x1F) == 0);  // no stray bits >= bit 5
}


// --- display_show()'s vertical layout --------------------------------------
// Both panel drivers place one or two centered lines with the same arithmetic.
// It used to be written out twice, once per driver, including the magic gap.

static void test_layout_lines_centers_a_single_line(void) {
    int y1 = -1, y2 = -1;
    display_layout_lines(64, false, &y1, &y2);
    CHECK(y1 == (64 - DISPLAY_FONT_GLYPH_HEIGHT) / 2);
}

static void test_layout_lines_centers_the_pair_as_a_block(void) {
    int y1 = -1, y2 = -1;
    display_layout_lines(64, true, &y1, &y2);
    int block = 2 * DISPLAY_FONT_GLYPH_HEIGHT + DISPLAY_FONT_LINE_GAP;
    CHECK(y1 == (64 - block) / 2);
    CHECK(y2 == y1 + DISPLAY_FONT_GLYPH_HEIGHT + DISPLAY_FONT_LINE_GAP);
    // The block is centered: the margin above line 1 equals the margin below
    // line 2, which is the property a reader actually cares about.
    CHECK(y1 == 64 - (y2 + DISPLAY_FONT_GLYPH_HEIGHT));
}

// A two-line screen sits higher than a one-line screen of the same text.
static void test_layout_lines_two_lines_start_above_one(void) {
    int one_y1 = -1, one_y2 = -1, two_y1 = -1, two_y2 = -1;
    display_layout_lines(240, false, &one_y1, &one_y2);
    display_layout_lines(240, true, &two_y1, &two_y2);
    CHECK(two_y1 < one_y1);
}

// --- centered string walk ---------------------------------------------------

struct capture { int n; int x[8]; int y[8]; char c[8]; };
static void record_glyph(int x, int y, const uint8_t *glyph, void *ctx) {
    struct capture *cap = ctx;
    (void)glyph;
    if (cap->n < 8) { cap->x[cap->n] = x; cap->y[cap->n] = y; cap->c[cap->n] = 'x'; cap->n++; }
}

static void test_draw_centered_emits_one_glyph_per_char_advancing_right(void) {
    struct capture cap = { 0, {0}, {0}, {0} };
    display_font_draw_centered("AB", 5, 64, record_glyph, &cap);
    CHECK(cap.n == 2);
    CHECK(cap.x[0] == display_layout_line("AB", 64));
    CHECK(cap.x[1] == cap.x[0] + DISPLAY_FONT_GLYPH_WIDTH);
    CHECK(cap.y[0] == 5);
    CHECK(cap.y[1] == 5);
}

// Text wider than the screen is skipped entirely rather than wrapped or cut
// mid-character — the same rule display_layout_line already encodes.
static void test_draw_centered_skips_text_too_wide_for_the_screen(void) {
    struct capture cap = { 0, {0}, {0}, {0} };
    display_font_draw_centered("ABCDEFGH", 0, 16, record_glyph, &cap);
    CHECK(cap.n == 0);
}

int main(void) {
    test_glyph_space();
    test_glyph_known_letter();
    test_glyph_first_and_last();
    test_glyph_out_of_range();
    test_glyph_distinct();
    test_layout_centers_short_text();
    test_layout_centers_empty_string();
    test_layout_exact_fit();
    test_layout_too_wide();
    test_downscale_fully_lit_glyph_stays_fully_lit();
    test_downscale_blank_glyph_stays_blank();
    test_downscale_preserves_a_single_top_left_pixel();
    test_downscale_output_width_never_exceeds_requested();
    test_layout_lines_centers_a_single_line();
    test_layout_lines_centers_the_pair_as_a_block();
    test_layout_lines_two_lines_start_above_one();
    test_draw_centered_emits_one_glyph_per_char_advancing_right();
    test_draw_centered_skips_text_too_wide_for_the_screen();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
