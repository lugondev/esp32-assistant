#pragma once
#include "board_types.h"

// Full-duplex single-I2S-controller audio (e.g. ESP32-C3). RX and TX share
// BCLK+WS on one controller; both ops back onto one allocation.
typedef struct {
    int bclk, ws;        // shared bit-clock + word-select (the speaker's pins)
    int mic_data;        // I2S data-in (INMP441)
    int spk_data;        // I2S data-out (MAX98357A)
    // Optional clock fan-out: when the mic is physically wired to its OWN
    // SCK/WS pins (not tied to the speaker's bclk/ws), set these to the mic's
    // SCK/WS gpios and the single controller's clock is duplicated onto them
    // via the GPIO matrix. Set to -1 when the mic shares bclk/ws physically
    // (no fan-out). UNVERIFIED on hardware.
    int mic_bclk, mic_ws;
} i2s_fd_cfg_t;

extern const mic_ops_t     i2s_fd_mic_ops;
extern const speaker_ops_t i2s_fd_speaker_ops;
