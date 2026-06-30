#pragma once
#include "esp_err.h"
#include <stdint.h>

esp_err_t audio_init(void);
int audio_mic_read(int16_t *pcm, int samples);
int audio_spk_write(const int16_t *pcm, int samples);
