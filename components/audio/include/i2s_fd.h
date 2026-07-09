#pragma once
#include "board_types.h"

// Full-duplex single-I2S-controller audio (e.g. ESP32-C3). RX and TX share
// BCLK+WS on one controller; both ops back onto one allocation.
typedef struct {
    int bclk, ws;        // shared bit-clock + word-select
    int mic_data;        // I2S data-in (INMP441)
    int spk_data;        // I2S data-out (MAX98357A)
} i2s_fd_cfg_t;

extern const mic_ops_t     i2s_fd_mic_ops;
extern const speaker_ops_t i2s_fd_speaker_ops;
