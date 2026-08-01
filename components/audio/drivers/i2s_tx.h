#pragma once
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>

// Everything the two speaker drivers do to a TX channel once it exists, minus
// the channel construction itself (which genuinely differs: a dedicated TX
// controller on dual-I2S SoCs, one shared with RX on single-I2S ones).
//
// i2s_speaker.c and i2s_fd.c used to carry line-for-line copies of the volume
// state, the Q8 scaling loop, the scratch chunking, the TX mutex, and the
// disable/enable reset — identical apart from the names of two constants.
//
// The mutex is not optional: two writers reach the speaker (spk_task playing
// the gateway's TTS, and voice_play announcing locally from status_task). They
// must not interleave on the channel, nor race on the scratch buffer.
typedef struct {
    i2s_chan_handle_t chan;
    SemaphoreHandle_t mutex;
    volatile int      volume;   // 0..100
} i2s_tx_t;

// Adopt an already-created, already-enabled TX channel and create the mutex.
// Returns ESP_FAIL if the mutex cannot be allocated.
esp_err_t i2s_tx_init(i2s_tx_t *tx, i2s_chan_handle_t chan, int initial_volume);

// Scale by the current volume and write, blocking until the DMA takes it all
// (portMAX_DELAY — the callers' real-time behaviour is built on this write
// blocking rather than dropping). Returns samples written, or -1 on error.
// At 100% the caller's buffer goes to the DMA untouched, no copy at all.
int i2s_tx_write(i2s_tx_t *tx, const int16_t *pcm, int samples);

// Discard whatever the DMA has already committed (barge-in): disable, then
// re-enable the channel.
void i2s_tx_reset(i2s_tx_t *tx);

void i2s_tx_set_volume(i2s_tx_t *tx, int pct);
int  i2s_tx_get_volume(const i2s_tx_t *tx);
int  i2s_tx_adjust_volume(i2s_tx_t *tx, int delta);
