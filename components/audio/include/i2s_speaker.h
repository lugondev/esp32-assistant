#pragma once
#include "board_types.h"

typedef struct {
    int port;             // I2S port number (MAX98357A TX)
    int bclk, lrc, din;   // I2S BCLK, LRCK, data-out pins
} i2s_speaker_cfg_t;

extern const speaker_ops_t i2s_speaker_ops;
