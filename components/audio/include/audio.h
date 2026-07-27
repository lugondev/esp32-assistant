#pragma once
#include "esp_err.h"
#include <stdint.h>

esp_err_t audio_init(void);
int audio_mic_read(int16_t *pcm, int samples);
// Discard whatever the mic's RX DMA has already captured, so the next read
// returns only audio recorded from this moment on. Used at every half-duplex
// transition back to LISTENING: without it the first uplink frame carries up to
// one frame of audio captured BEFORE the transition — which, right after the
// bot spoke, is the bot's own voice, and the server transcribes it as user
// speech (self-talk).
// No-op on boards whose mic driver doesn't implement it.
// MUST be called only from the task that calls audio_mic_read() — see
// mic_ops_t.flush in board_types.h.
void audio_mic_flush(void);
int audio_spk_write(const int16_t *pcm, int samples);
// Drop any audio already committed to the I2S TX DMA (barge-in): the current
// utterance stops within one DMA buffer instead of playing out. Safe to call
// from any task; serialized against audio_spk_write via the TX mutex.
void audio_spk_reset(void);

// Software output volume (MAX98357A has no hardware volume control), applied
// by scaling PCM samples in audio_spk_write(). Range 0..100 (100 = passthrough).
void audio_set_volume(int pct);
int  audio_get_volume(void);
// Adjust by delta (clamped to 0..100); returns the new volume.
int  audio_adjust_volume(int delta);
