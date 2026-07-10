#pragma once
#include <stdint.h>
#include <stdbool.h>

// Pure, host-testable status bar logic + renderer: WiFi signal bars, a
// centered text label, and a battery icon. No hardware access.

// Signal bars [0,4] for the given connection state. Thresholds are the same
// ones commonly used elsewhere (>=-55dBm excellent .. <-75dBm weak); 0 when
// not connected regardless of a stale rssi_dbm value.
int statusbar_wifi_bars(bool connected, int rssi_dbm);

// Renders into buf (buf_w * buf_h RGB565): WiFi bars at the left, `text`
// horizontally centered (8x8 font, silently skipped if too wide to fit,
// same rule as display_layout_line), and a battery icon at the right.
// battery_pct: 0-100 fills the icon proportionally; pass a negative value to
// omit the battery icon entirely (e.g. a board with no battery sensor).
void statusbar_render(uint16_t *buf, int buf_w, int buf_h,
                       int wifi_bars, const char *text, int battery_pct,
                       uint16_t fg, uint16_t bg);
