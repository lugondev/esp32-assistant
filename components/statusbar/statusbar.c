#include "statusbar.h"
#include "gfx.h"
#include "display_font.h"
#include "wifi_signal.h"
#include <string.h>

int statusbar_wifi_bars(bool connected, int rssi_dbm) {
    // The thresholds themselves live in wifi_signal_bars — the setup portal
    // renders the same ladder and the two used to keep private copies of it.
    // All this layer adds is the "not associated" case the portal has no use
    // for (a scan result is reachable by definition).
    return connected ? wifi_signal_bars(rssi_dbm) : 0;
}

// Below this status-bar height, the full 8x8 font doesn't leave enough
// margin around itself (see main.c's status_bar_height on a 128x64 SSD1306)
// — fall back to a downscaled 5x7 glyph instead of just accepting a
// cramped/edge-to-edge 8px-tall line of text.
#define COMPACT_THRESHOLD_BUF_H 16
#define COMPACT_GLYPH_W 5
#define COMPACT_GLYPH_H 7

static void glyph_size(int buf_h, int *gw, int *gh) {
    bool compact = buf_h < COMPACT_THRESHOLD_BUF_H;
    *gw = compact ? COMPACT_GLYPH_W : DISPLAY_FONT_GLYPH_WIDTH;
    *gh = compact ? COMPACT_GLYPH_H : DISPLAY_FONT_GLYPH_HEIGHT;
}

static void draw_text(uint16_t *buf, int buf_w, int buf_h,
                       int x, int y, const char *text, uint16_t fg) {
    int gw, gh;
    glyph_size(buf_h, &gw, &gh);
    bool compact = gw != DISPLAY_FONT_GLYPH_WIDTH;
    for (const char *p = text; *p; p++) {
        const uint8_t *glyph = display_font_glyph(*p);
        if (glyph) {
            uint8_t small[8];
            if (compact) {
                display_font_downscale(glyph, gw, gh, small);
                glyph = small;
            }
            gfx_blit_bitmap1(buf, buf_w, buf_h, x, y, glyph, gw, gh, fg);
        }
        x += gw;   // advance even for an unmapped character, so the layout
                   // statusbar_render measured with strlen still holds
    }
}

void statusbar_render(uint16_t *buf, int buf_w, int buf_h,
                       int wifi_bars, const char *text, int battery_pct,
                       bool charging, uint16_t fg, uint16_t bg) {
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
    int gw, gh;
    glyph_size(buf_h, &gw, &gh);
    int text_w = (int)strlen(text) * gw;
    if (text_w <= buf_w) {
        int tx = (buf_w - text_w) / 2;
        int ty = (buf_h - gh) / 2;
        draw_text(buf, buf_w, buf_h, tx, ty, text, fg);
    }

    // Battery icon geometry is computed unconditionally (not just when
    // battery_pct >= 0) so the charging bolt below has a stable anchor even
    // on a board that knows charge state (TP4056 CHRG/STDBY pins) but has no
    // voltage-divider reading yet.
    int body_w = 18, body_h = 9, nub_w = 2, nub_h = 4;
    int bx = buf_w - body_w - nub_w - 4;
    int by = (buf_h - body_h) / 2;

    // Battery: outline (via nested rounded rects — no separate stroke
    // primitive exists) + a nub, filled proportionally to battery_pct.
    // Negative battery_pct means no battery sensor on this board — omit
    // the icon entirely rather than show a meaningless fixed value.
    if (battery_pct >= 0) {
        int pct = battery_pct > 100 ? 100 : battery_pct;
        gfx_fill_rounded_rect(buf, buf_w, buf_h, bx, by, body_w, body_h, 2, fg);
        gfx_fill_rounded_rect(buf, buf_w, buf_h, bx + 1, by + 1, body_w - 2, body_h - 2, 1, bg);
        int fill_w = ((body_w - 4) * pct) / 100;
        if (fill_w > 0) gfx_fill_rect(buf, buf_w, buf_h, bx + 2, by + 2, fill_w, body_h - 4, fg);
        gfx_fill_rect(buf, buf_w, buf_h, bx + body_w, by + (body_h - nub_h) / 2, nub_w, nub_h, fg);
    }

    // Charging bolt: a small right-pointing triangle just left of the
    // battery icon's position. Independent of battery_pct on purpose.
    if (charging) {
        int blt_x = bx - 8;
        gfx_fill_triangle(buf, buf_w, buf_h,
                           blt_x, by, blt_x, by + body_h, blt_x + 5, by + body_h / 2,
                           fg);
    }
}
