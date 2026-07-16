#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Brings up the SPI bus + ST7789 panel (SCLK=42, MOSI=41, DC=1, RST=2,
// no CS/MISO, 240x240, no offset) and turns the backlight (GPIO17) on.
// Clears the screen to black. Must be called once before display_show().
esp_err_t display_init(void);

// Clears the screen and draws up to two lines of text, each horizontally
// centered, using the 8x8 font. `line2` may be NULL to show a single line
// (vertically centered on its own in that case). Any line wider than the
// screen is silently skipped (not drawn) rather than wrapped or truncated
// mid-character.
void display_show(const char *line1, const char *line2);

// Blits an RGB565 buffer (w*h pixels, row-major) to the panel at (x,y).
// Used for images and per-frame animation (e.g. robot_eyes) instead of the
// text path. Same task-affinity rule as display_show(): only status_task
// may call this (see the isolation comment in main.c).
void display_flush(int x, int y, int w, int h, const uint16_t *rgb565);

// Panel dimensions in pixels, from the active board's display_ops.
int display_width(void);
int display_height(void);

// True if the panel is 1-bit (see display_ops_t.mono): everything
// display_flush() is handed gets thresholded to on/off by luminance, so
// gradients and dimmed tones can't survive the trip.
bool display_is_mono(void);

// Turn the panel backlight on/off (GPIO, no PWM — see st7789 driver comment).
void display_set_backlight(bool on);
