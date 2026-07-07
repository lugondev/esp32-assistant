#include "display_font.h"
#include <string.h>
#include <stdio.h>

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
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
