#pragma once
#include "board.h"
#include "display_auto.h"
#include "buttons_gpio.h"

// Boilerplate shared by every components/boards/<name>/board_def.c.
//
// A board file should read as a pin map and nothing else. Before this header,
// each of them also carried the same three declarations verbatim — the two
// panel cfg structs plus the display_auto_cfg that pairs them, the reserved-pin
// array's sizeof arithmetic, and (in three of the five) an identical copy of
// all of it. The macros below keep the pin numbers at the call site, where they
// belong, and take the mechanical parts out of view.

// Declares ssd1306_cfg, st7789_cfg and display_cfg for the usual arrangement:
// one physical pin pair carrying either I2C (SSD1306) or SPI (ST7789), with the
// ST7789 additionally using dc/rst/bl. Pass bl = -1 where the panel's LED is
// tied to 3V3 and there is no GPIO to drive.
//
// The board then sets `.display = NULL, .display_cfg = &display_cfg` and
// display_init() probes for the OLED, falling back to the TFT. A board that can
// only ever carry one of the two should write the structs out by hand and leave
// the other sub-cfg NULL — display_auto_cfg_t supports that, this macro does
// not try to.
#define LUGO_DISPLAY_AUTO(clk_pin, dat_pin, dc_pin, rst_pin, bl_pin)          \
    static const display_ssd1306_cfg_t ssd1306_cfg = {                        \
        .scl = (clk_pin), .sda = (dat_pin), .i2c_addr = 0x3C,                 \
    };                                                                        \
    static const display_st7789_cfg_t st7789_cfg = {                          \
        .sclk = (clk_pin), .mosi = (dat_pin),                                 \
        .dc = (dc_pin), .rst = (rst_pin), .bl = (bl_pin),                     \
    };                                                                        \
    static const display_auto_cfg_t display_cfg = {                           \
        .ssd1306 = &ssd1306_cfg, .st7789 = &st7789_cfg,                       \
    }

// Declares the board's reserved-pin list. List the pins the BOARD wired that no
// IDF driver reserves: the display bus, DC/RST/backlight, and the buttons. Do
// NOT list the I2S mic/speaker pins — the I2S driver reserves those itself at
// channel init, so esp_gpio_is_reserved() already covers them and a second copy
// here would just be one more thing to keep in sync (see board_t.reserved_pins).
//
// Always pass the same named constants the cfg structs above use, never fresh
// literals: a hand-written second copy of the pin numbers is exactly what let
// this list go stale against a board's real pinout once already.
#define LUGO_RESERVED_PINS(...)                                               \
    static const int reserved_pins[] = { __VA_ARGS__ }

// Wires the array declared by LUGO_RESERVED_PINS into a LUGO_BOARD_REGISTER
// body. Exists so the sizeof division is written once rather than five times.
#define LUGO_BOARD_RESERVED_PINS                                              \
    .reserved_pins   = reserved_pins,                                         \
    .n_reserved_pins = (int)(sizeof(reserved_pins) / sizeof(reserved_pins[0]))
