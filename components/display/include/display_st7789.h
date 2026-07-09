#pragma once
#include "board_types.h"

typedef struct {
    int sclk, mosi, dc, rst, bl;   // ST7789 SPI pins + backlight
} display_st7789_cfg_t;

extern const display_ops_t display_st7789_ops;
