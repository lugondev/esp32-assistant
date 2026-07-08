#pragma once
#include "esp_err.h"
#include <stdint.h>

#define OPUS_UP_RATE      16000
#define OPUS_DOWN_RATE    16000
#define OPUS_FRAME_MS     60
#define OPUS_UP_SAMPLES   960    // 16000 * 0.06 (uplink: we always send 60ms frames)
// Decode buffer must hold the LARGEST Opus frame the gateway may send, not
// just our 60ms uplink size. A single Opus packet can carry up to 120ms
// (1920 samples @ 16kHz) — the reference RPi client (scripts/rpi_voice_client.py)
// sizes its decode buffer as OUT_RATE*120/1000 for exactly this reason.
// Undersizing this (was 960) let opus_decode smash the caller's stack when a
// >60ms frame arrived, corrupting memory that crashed elsewhere later.
#define OPUS_DOWN_SAMPLES     960     // nominal 60ms frame
#define OPUS_DOWN_SAMPLES_MAX 1920    // 16000 * 0.12 — max decodable Opus frame
#define OPUS_MAX_PACKET   1500

esp_err_t opus_codec_init(void);
int opus_codec_encode(const int16_t *pcm960, uint8_t *out, int out_cap);
int opus_codec_decode(const uint8_t *pkt, int pkt_len, int16_t *pcm_out);
// Reset the decoder's internal state (call on barge-in/turn abort so a new
// reply doesn't decode against stale inter-frame state, which clicks/warbles).
void opus_codec_reset(void);
