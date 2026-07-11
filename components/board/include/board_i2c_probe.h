#pragma once
#include <stdbool.h>
#include <stdint.h>

// Briefly brings up scl/sda as a throwaway I2C master bus and checks for an
// ACK at addr, then tears the bus back down. For use from board_def.c
// match() functions at board-selection time — before any board is chosen
// and before any peripheral driver claims these pins for real. Safe to call
// even when the pins are actually wired to a different bus (e.g. an
// ST7789's SPI clock/data): whichever board wins re-initializes its own
// hardware afterward, and ST7789 specifically gets a real RST-pin reset
// before any init command is sent, clearing whatever this probe's clocking
// left behind.
bool board_i2c_probe(int scl, int sda, uint16_t addr, int timeout_ms);
