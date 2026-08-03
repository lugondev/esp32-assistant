#pragma once
#include <stdbool.h>
#include <stdint.h>

// Briefly brings up scl/sda as a throwaway I2C master bus and checks for an
// ACK at addr, then tears the bus back down.
//
// The one caller is display_init() (components/display/display.c), which uses
// it to pick the panel on a board that declares .display = NULL: probe the
// shared clock/data pins for an SSD1306, else drive an ST7789 on them. So it
// runs *after* the board is selected, but still before any panel driver has
// claimed those pins. Safe to call even when they turn out to carry SPI rather
// than I2C: the losing branch is never initialized, and the ST7789 branch
// issues a real RST-pin reset before its first init command, clearing whatever
// this probe's clocking left behind.
bool board_i2c_probe(int scl, int sda, uint16_t addr, int timeout_ms);
