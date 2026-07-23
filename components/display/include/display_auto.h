#pragma once
#include "display_ssd1306.h"
#include "display_st7789.h"

// Auto-detected display. A board sets board_t.display = NULL and points
// board_t.display_cfg at a display_auto_cfg_t; display_init() then probes I2C
// for the SSD1306 and uses it if it ACKs, otherwise falls back to the ST7789
// (SPI). Either sub-cfg may be NULL when the board has no such panel option
// (e.g. an SSD1306-only board leaves .st7789 = NULL). This keeps display panel
// selection in one place (the display layer) so every board is identical —
// each just declares the pins for the panel(s) it can carry.
typedef struct {
    const display_ssd1306_cfg_t *ssd1306;   // I2C OLED option, or NULL
    const display_st7789_cfg_t  *st7789;    // SPI TFT option, or NULL
} display_auto_cfg_t;
