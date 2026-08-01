#include "i2s_speaker.h"
#include "i2s_tx.h"
#include "driver/i2s_std.h"
#include "soc/soc_caps.h"
#include "esp_log.h"

#if SOC_I2S_NUM > 1   // dedicated TX controller (dual-I2S SoCs, e.g. ESP32-S3)

static const char *TAG = "i2s_speaker";

// Volume, mutex and the scaled chunked write all live in i2s_tx (shared with
// the single-I2S driver); this file owns only the channel bring-up.
#define SPK_DEFAULT_VOLUME 80
static i2s_tx_t s_tx;

static void spk_set_volume(int pct) { i2s_tx_set_volume(&s_tx, pct); }
static int  spk_get_volume(void) { return i2s_tx_get_volume(&s_tx); }
static int  spk_adjust_volume(int delta) { return i2s_tx_adjust_volume(&s_tx, delta); }

static esp_err_t spk_init(const void *cfg_v) {
    const i2s_speaker_cfg_t *c = (const i2s_speaker_cfg_t *)cfg_v;
    // MAX98357A takes standard 16-bit I2S directly, no bit-shift on the way out.
    i2s_chan_config_t tx_cc = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)c->port, I2S_ROLE_MASTER);
    // Underrun (playback drained, e.g. the gap while the gateway synthesizes the
    // next sentence) emits silence instead of looping the last DMA buffer — else
    // the last syllable repeats ("...tán gẫu ấu ấu ấu..."). Pairs with a downlink
    // queue deep enough to hold a whole sentence burst (DL_QUEUE_DEPTH) so frames
    // aren't dropped mid-sentence. Same idea as the C3 i2s_fd driver.
    tx_cc.auto_clear = true;
    i2s_chan_handle_t chan;
    ESP_ERROR_CHECK(i2s_new_channel(&tx_cc, &chan, NULL));
    i2s_std_config_t tx_std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = c->bclk,
            .ws = c->lrc, .dout = c->din, .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(chan, &tx_std));
    ESP_ERROR_CHECK(i2s_channel_enable(chan));

    esp_err_t err = i2s_tx_init(&s_tx, chan, SPK_DEFAULT_VOLUME);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "speaker ready");
    return ESP_OK;
}

static int spk_write(const int16_t *pcm, int samples) {
    return i2s_tx_write(&s_tx, pcm, samples);
}

static void spk_reset(void) { i2s_tx_reset(&s_tx); }

const speaker_ops_t i2s_speaker_ops = {
    .init = spk_init, .write = spk_write, .reset = spk_reset,
    .set_volume = spk_set_volume, .get_volume = spk_get_volume, .adjust_volume = spk_adjust_volume,
};

#endif // SOC_I2S_NUM > 1
