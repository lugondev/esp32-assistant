#pragma once
#include "board_types.h"

typedef struct {
    int mic_ws, mic_sck, mic_sd;      // INMP441 I2S RX pins
    int spk_bclk, spk_lrc, spk_din;   // MAX98357A I2S TX pins
} audio_i2s_cfg_t;

extern const audio_ops_t audio_i2s_ops;
