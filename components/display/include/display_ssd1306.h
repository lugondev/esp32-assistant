#pragma once
#include "board_types.h"
#include <stdint.h>

// SSD1306 over I2C (4-pin module: VCC/GND/SCL/SDA — no DC/RST, so the panel
// has no hardware reset line; D/C selection is encoded in the I2C protocol's
// control byte instead of a dedicated pin).
typedef struct {
    int sda, scl;
    uint16_t i2c_addr;   // typically 0x3C, sometimes 0x3D
} display_ssd1306_cfg_t;

extern const display_ops_t display_ssd1306_ops;
