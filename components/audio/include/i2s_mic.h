#pragma once
#include "board_types.h"

typedef struct {
    int port;          // I2S port number (INMP441 RX)
    int ws, sck, sd;   // I2S LRCK, BCLK, data-in pins
} i2s_mic_cfg_t;

extern const mic_ops_t i2s_mic_ops;
