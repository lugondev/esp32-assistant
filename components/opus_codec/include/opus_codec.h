#pragma once
#include "esp_err.h"
#include <stdint.h>

#define OPUS_UP_RATE      16000
#define OPUS_DOWN_RATE    24000
#define OPUS_FRAME_MS     60
#define OPUS_UP_SAMPLES   960    // 16000 * 0.06
#define OPUS_DOWN_SAMPLES 1440   // 24000 * 0.06
#define OPUS_MAX_PACKET   1500

esp_err_t opus_codec_init(void);
int opus_codec_encode(const int16_t *pcm960, uint8_t *out, int out_cap);
int opus_codec_decode(const uint8_t *pkt, int pkt_len, int16_t *pcm1440);
