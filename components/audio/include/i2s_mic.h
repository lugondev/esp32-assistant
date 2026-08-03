#pragma once
#include "board_types.h"

typedef struct {
    int port;          // I2S port number (INMP441 RX)
    int ws, sck, sd;   // I2S LRCK, BCLK, data-in pins
    // Which I2S slot the mic actually drives, i.e. how the board wired the
    // INMP441's L/R pin: false (the default, and what every board using L/R->GND
    // wants) = left slot, true = L/R tied high, data in the right slot.
    // A board that leaves this out gets the left slot, so this is additive —
    // it exists because a board was found wired the other way, and reading the
    // wrong slot yields a channel of pure zeros that looks exactly like a dead
    // mic. Zero-initialised by designated-initialiser boards that omit it.
    bool right_slot;
} i2s_mic_cfg_t;

extern const mic_ops_t i2s_mic_ops;
