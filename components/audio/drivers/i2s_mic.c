#include "i2s_mic.h"
#include "i2s_pcm.h"
#include "driver/i2s_std.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#if SOC_I2S_NUM > 1   // dedicated RX controller (dual-I2S SoCs, e.g. ESP32-S3)

static const char *TAG = "i2s_mic";
static i2s_chan_handle_t s_rx;

// Largest samples value any caller passes (mic_task reads OPUS_UP_SAMPLES == 960).
// Fixed buffer (not a VLA) to keep stack usage bounded.
#define MIC_MAX_SAMPLES 960

// Board gain, applied as an arithmetic right shift on the mic's 32-bit
// left-justified slot (see i2s_pcm.h). The INMP441 delivers ~18-bit-deep
// samples, so a straight >>16 leaves speech near -60 dBFS; 11 adds ~+30 dB.
// One step is 6 dB. This is the value the S3 boards have been tuned and
// hardware-validated at — the C3 driver deliberately declares its own (see
// i2s_fd.c's FD_MIC_GAIN_SHIFT), which is why the shift is a per-driver
// constant rather than a shared one.
#define MIC_GAIN_SHIFT 11

static esp_err_t mic_init(const void *cfg_v) {
    const i2s_mic_cfg_t *c = (const i2s_mic_cfg_t *)cfg_v;
    // INMP441 outputs 24-bit samples left-justified in a 32-bit I2S frame (it
    // always clocks 32 SCK per WS half-period), so the RX channel must run at
    // 32-bit slot width or the mic reads garbage.
    // MONO/LEFT, not STEREO: the INMP441 drives only the left slot (L/R tied
    // low), so a STEREO channel spent half its DMA bandwidth and half of
    // raw[] carrying the empty right slot — 7680 B moved per 60ms frame to
    // deliver 3840 B of audio. The C3's i2s_fd driver has always read this
    // same mic as 32-bit MONO, so this is adopting a config already proven on
    // hardware in this repo, not a new experiment. slot_mask is set explicitly
    // rather than left to the driver's mono default so the choice of slot is
    // visible at the call site.
    i2s_chan_config_t rx_cc = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)c->port, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_cc, NULL, &s_rx));
    i2s_std_config_t rx_std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = c->sck,
            .ws = c->ws, .dout = I2S_GPIO_UNUSED, .din = c->sd,
        },
    };
    rx_std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;   // INMP441 with L/R -> GND
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &rx_std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
    ESP_LOGI(TAG, "mic ready");
    return ESP_OK;
}

static int mic_read(int16_t *pcm, int samples) {
    if (samples > MIC_MAX_SAMPLES) samples = MIC_MAX_SAMPLES;
    static int32_t raw[MIC_MAX_SAMPLES];   // MONO: one 32-bit slot per sample
    size_t bytes_read = 0;
    // portMAX_DELAY: this channel has a dedicated RX controller and always
    // clocks, so the read returns as soon as a full frame is in the DMA. The
    // mic_ops_t contract allows either this or a timing-out short read (the
    // single-I2S driver does the latter).
    esp_err_t err = i2s_channel_read(s_rx, raw, samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    if (err != ESP_OK) return -1;
    int frames = (int)(bytes_read / sizeof(int32_t));
    i2s_pcm_from_i2s32(raw, frames, MIC_GAIN_SHIFT, pcm);
    return frames;
}

// Restart the RX channel to throw away everything the DMA has captured so far.
// Callable ONLY from the task that calls mic_read (see mic_ops_t.flush).
static void mic_flush(void) {
    i2s_channel_disable(s_rx);
    i2s_channel_enable(s_rx);
}

const mic_ops_t i2s_mic_ops = { .init = mic_init, .read = mic_read, .flush = mic_flush };

#endif // SOC_I2S_NUM > 1
