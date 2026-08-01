#include "i2s_tx.h"
#include "i2s_pcm.h"
#include "esp_log.h"

static const char *TAG = "i2s_tx";

// Scratch for the scaled copy. One buffer for both drivers is safe: only one
// of them is compiled in per target (they are guarded on SOC_I2S_NUM), and
// within a target every write goes through the caller's mutex.
#define TX_SCRATCH_SAMPLES 512

esp_err_t i2s_tx_init(i2s_tx_t *tx, i2s_chan_handle_t chan, int initial_volume) {
    tx->chan = chan;
    tx->volume = i2s_pcm_clamp_volume(initial_volume);
    tx->mutex = xSemaphoreCreateMutex();
    if (!tx->mutex) { ESP_LOGE(TAG, "mutex create failed"); return ESP_FAIL; }
    return ESP_OK;
}

int i2s_tx_write(i2s_tx_t *tx, const int16_t *pcm, int samples) {
    int vol = tx->volume;   // sampled once: a concurrent volume change must not
                            // split this buffer across two gains
    size_t total = 0;
    esp_err_t err = ESP_OK;

    xSemaphoreTake(tx->mutex, portMAX_DELAY);
    if (vol >= 100) {
        // Nothing to scale — hand the caller's buffer straight to the DMA.
        size_t bw = 0;
        err = i2s_channel_write(tx->chan, pcm, samples * sizeof(int16_t), &bw, portMAX_DELAY);
        total = bw;
    } else {
        static int16_t scratch[TX_SCRATCH_SAMPLES];
        int gain_q8 = i2s_pcm_gain_q8(vol);
        int off = 0;
        while (off < samples) {
            int chunk = samples - off;
            if (chunk > TX_SCRATCH_SAMPLES) chunk = TX_SCRATCH_SAMPLES;
            i2s_pcm_apply_gain(pcm + off, scratch, chunk, gain_q8);
            size_t bw = 0;
            err = i2s_channel_write(tx->chan, scratch, chunk * sizeof(int16_t), &bw, portMAX_DELAY);
            total += bw;
            if (err != ESP_OK) break;
            off += chunk;
        }
    }
    xSemaphoreGive(tx->mutex);

    if (err != ESP_OK) return -1;
    return (int)(total / sizeof(int16_t));
}

void i2s_tx_reset(i2s_tx_t *tx) {
    xSemaphoreTake(tx->mutex, portMAX_DELAY);
    i2s_channel_disable(tx->chan);
    i2s_channel_enable(tx->chan);
    xSemaphoreGive(tx->mutex);
}

void i2s_tx_set_volume(i2s_tx_t *tx, int pct) {
    tx->volume = i2s_pcm_clamp_volume(pct);
}

int i2s_tx_get_volume(const i2s_tx_t *tx) { return tx->volume; }

int i2s_tx_adjust_volume(i2s_tx_t *tx, int delta) {
    tx->volume = i2s_pcm_clamp_volume(tx->volume + delta);
    return tx->volume;
}
