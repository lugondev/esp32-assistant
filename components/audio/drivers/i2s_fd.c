#include "i2s_fd.h"
#include "i2s_pcm.h"
#include "i2s_tx.h"
#include "driver/i2s_std.h"
#include "soc/soc_caps.h"
#include "esp_log.h"

#if SOC_I2S_NUM == 1   // single-I2S SoCs (e.g. ESP32-C3): mic + speaker as two
                       // independent simplex channels on the one controller.

static const char *TAG = "i2s_fd";
static i2s_chan_handle_t s_rx;
// Volume, mutex and the scaled chunked write live in i2s_tx, shared with the
// dual-I2S driver; this file owns the RX side and the channel bring-up.
static i2s_tx_t s_tx;
static bool s_ready;   // guards init (both mic->init and speaker->init call it)

#define FD_MIC_MAX_SAMPLES 960
#define FD_SPK_DEFAULT_VOLUME 80

// Board gain for the mic's 32-bit left-justified slot (see i2s_pcm.h). 12 is
// what the C3 boards were brought up and validated at (xiaozhi uses the same
// value on this wiring); the dual-I2S S3 driver declares 11, i.e. 6 dB louder.
// Whether the two SHOULD converge is a hardware-tuning question — the point of
// naming it here is that the difference is now a visible, deliberate per-board
// constant instead of a divergence hidden in two copy-pasted loops.
#define FD_MIC_GAIN_SHIFT 12

// Allocate both channels once. Idempotent: mic->init and speaker->init both
// call it; init order does not matter.

static esp_err_t fd_ensure_init(const void *cfg_v) {
    if (s_ready) return ESP_OK;
    const i2s_fd_cfg_t *c = (const i2s_fd_cfg_t *)cfg_v;
    // TWO independent simplex channels on the one I2S controller (like xiaozhi's
    // NoAudioCodecSimplex): a TX-only channel on the speaker's pins and an
    // RX-only channel on the mic's OWN pins, each master, each generating its own
    // BCLK/WS. Shared-clock full-duplex left the C3 RX unsampled (mic read 0);
    // independent simplex is what works.
    // Because the two channels are independent they do NOT have to share a slot
    // width — an earlier revision did share one (32-bit for both) back when they
    // shared BCLK/WS, and that constraint outlived the design. So:
    //   RX 32-bit MONO/LEFT — required, the INMP441 always clocks 32 SCK per WS
    //     half-period and delivers 24-bit left-justified data.
    //   TX 16-bit MONO — the MAX98357A takes 16-bit I2S directly (this is exactly
    //     what the dual-I2S S3 driver has always used). Halves the TX DMA bytes
    //     and drops the per-sample "<<16 into a 32-bit slot" repack entirely.
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;      // TX underrun -> silence, not stale garbage
    cc.dma_desc_num = 8;

    // TX (MAX98357A speaker) on its own bclk/ws.
    i2s_chan_handle_t tx_chan;
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &tx_chan, NULL));
    i2s_std_config_t tx_std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = c->bclk, .ws = c->ws,
                      .dout = c->spk_data, .din = I2S_GPIO_UNUSED },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

    // RX (INMP441 mic) on ITS OWN bclk/ws (mic_bclk/mic_ws) — separate pins,
    // and its own 32-bit slot width (see above). The slot macro expands to a
    // brace initializer, so it needs a named variable here rather than a plain
    // assignment into rx_std.
    const i2s_std_slot_config_t rx_slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
    ESP_ERROR_CHECK(i2s_new_channel(&cc, NULL, &s_rx));
    i2s_std_config_t rx_std = tx_std;
    rx_std.slot_cfg = rx_slot;
    rx_std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;   // INMP441 with L/R -> GND
    rx_std.gpio_cfg.bclk = c->mic_bclk;
    rx_std.gpio_cfg.ws   = c->mic_ws;
    rx_std.gpio_cfg.dout = I2S_GPIO_UNUSED;
    rx_std.gpio_cfg.din  = c->mic_data;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &rx_std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));

    esp_err_t err = i2s_tx_init(&s_tx, tx_chan, FD_SPK_DEFAULT_VOLUME);
    if (err != ESP_OK) return err;
    s_ready = true;
    ESP_LOGI(TAG, "i2s simplex ready (tx=%d/%d rx=%d/%d)", c->bclk, c->ws, c->mic_bclk, c->mic_ws);
    return ESP_OK;
}

static esp_err_t fd_mic_init(const void *cfg) { return fd_ensure_init(cfg); }
static esp_err_t fd_spk_init(const void *cfg) { return fd_ensure_init(cfg); }

static int fd_mic_read(int16_t *pcm, int samples) {
    if (samples > FD_MIC_MAX_SAMPLES) samples = FD_MIC_MAX_SAMPLES;
    static int32_t raw[FD_MIC_MAX_SAMPLES];   // MONO: one 32-bit slot per sample
    size_t br = 0;
    // Bounded wait, unlike the dual-I2S driver's portMAX_DELAY: the RX channel
    // here shares its controller with TX, so a stall must not park mic_task
    // forever. A timeout surfaces as a short read, which the mic_ops_t contract
    // explicitly permits.
    if (i2s_channel_read(s_rx, raw, samples * sizeof(int32_t), &br, pdMS_TO_TICKS(200)) != ESP_OK)
        return -1;   // per mic_ops_t: -1 is an error, 0 is a legitimate empty read
    int frames = (int)(br / sizeof(int32_t));
    i2s_pcm_from_i2s32(raw, frames, FD_MIC_GAIN_SHIFT, pcm);
    return frames;
}

static void fd_set_volume(int pct) { i2s_tx_set_volume(&s_tx, pct); }
static int  fd_get_volume(void) { return i2s_tx_get_volume(&s_tx); }
static int  fd_adjust_volume(int d) { return i2s_tx_adjust_volume(&s_tx, d); }

// 16-bit MONO, matching the TX slot_cfg: the PCM goes to the MAX98357A as-is,
// with no repack into 32-bit slots (see fd_ensure_init).
static int fd_spk_write(const int16_t *pcm, int samples) {
    return i2s_tx_write(&s_tx, pcm, samples);
}

static void fd_spk_reset(void) { i2s_tx_reset(&s_tx); }

// Restart the RX channel to throw away everything the DMA has captured so far.
// Callable ONLY from the task that calls fd_mic_read (see mic_ops_t.flush).
// No TX mutex here: this touches s_rx only, which the speaker path never uses.
static void fd_mic_flush(void) {
    i2s_channel_disable(s_rx);
    i2s_channel_enable(s_rx);
}

const mic_ops_t i2s_fd_mic_ops = { .init = fd_mic_init, .read = fd_mic_read, .flush = fd_mic_flush };
const speaker_ops_t i2s_fd_speaker_ops = {
    .init = fd_spk_init, .write = fd_spk_write, .reset = fd_spk_reset,
    .set_volume = fd_set_volume, .get_volume = fd_get_volume, .adjust_volume = fd_adjust_volume,
};

#endif // SOC_I2S_NUM == 1
