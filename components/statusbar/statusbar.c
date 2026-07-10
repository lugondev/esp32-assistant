#include "statusbar.h"
#include "gfx.h"
#include "display_font.h"
#include <string.h>

int statusbar_wifi_bars(bool connected, int rssi_dbm) {
    if (!connected) return 0;
    if (rssi_dbm >= -55) return 4;
    if (rssi_dbm >= -65) return 3;
    if (rssi_dbm >= -75) return 2;
    return 1;
}

static void draw_text(uint16_t *buf, int buf_w, int buf_h,
                       int x, int y, const char *text, uint16_t fg) {
    for (const char *p = text; *p; p++) {
        const uint8_t *glyph = display_font_glyph(*p);
        if (glyph) {
            for (int row = 0; row < DISPLAY_FONT_GLYPH_HEIGHT; row++) {
                uint8_t bits = glyph[row];
                for (int col = 0; col < DISPLAY_FONT_GLYPH_WIDTH; col++) {
                    if ((bits >> col) & 1) {
                        int px = x + col, py = y + row;
                        if (px >= 0 && px < buf_w && py >= 0 && py < buf_h)
                            buf[py * buf_w + px] = fg;
                    }
                }
            }
        }
        x += DISPLAY_FONT_GLYPH_WIDTH;
    }
}

void statusbar_render(uint16_t *buf, int buf_w, int buf_h,
                       int wifi_bars, const char *text, int battery_pct,
                       uint16_t fg, uint16_t bg) {
    gfx_fill_rect(buf, buf_w, buf_h, 0, 0, buf_w, buf_h, bg);

    // WiFi bars: 4 columns of increasing height, bottom-aligned, left margin.
    int bar_w = 3, bar_gap = 1, bar_x0 = 4;
    for (int i = 0; i < 4; i++) {
        if (i >= wifi_bars) continue;   // unfilled bars stay background
        int bar_h = (i + 1) * 2;
        int bx = bar_x0 + i * (bar_w + bar_gap);
        int by = buf_h - 2 - bar_h;
        gfx_fill_rect(buf, buf_w, buf_h, bx, by, bar_w, bar_h, fg);
    }

    // Centered text (same centering rule as display_layout_line: skip if
    // wider than the bar rather than wrap/truncate mid-character).
    int text_w = (int)strlen(text) * DISPLAY_FONT_GLYPH_WIDTH;
    if (text_w <= buf_w) {
        int tx = (buf_w - text_w) / 2;
        int ty = (buf_h - DISPLAY_FONT_GLYPH_HEIGHT) / 2;
        draw_text(buf, buf_w, buf_h, tx, ty, text, fg);
    }

    // Battery: outline (via nested rounded rects — no separate stroke
    // primitive exists) + a nub, filled proportionally to battery_pct.
    // Negative battery_pct means no battery sensor on this board — omit
    // the icon entirely rather than show a meaningless fixed value.
    if (battery_pct >= 0) {
        int pct = battery_pct > 100 ? 100 : battery_pct;
        int body_w = 18, body_h = 9, nub_w = 2, nub_h = 4;
        int bx = buf_w - body_w - nub_w - 4;
        int by = (buf_h - body_h) / 2;
        gfx_fill_rounded_rect(buf, buf_w, buf_h, bx, by, body_w, body_h, 2, fg);
        gfx_fill_rounded_rect(buf, buf_w, buf_h, bx + 1, by + 1, body_w - 2, body_h - 2, 1, bg);
        int fill_w = ((body_w - 4) * pct) / 100;
        if (fill_w > 0) gfx_fill_rect(buf, buf_w, buf_h, bx + 2, by + 2, fill_w, body_h - 4, fg);
        gfx_fill_rect(buf, buf_w, buf_h, bx + body_w, by + (body_h - nub_h) / 2, nub_w, nub_h, fg);
    }
}
