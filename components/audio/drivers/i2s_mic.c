#include "i2s_mic.h"
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

static esp_err_t mic_init(const void *cfg_v) {
    const i2s_mic_cfg_t *c = (const i2s_mic_cfg_t *)cfg_v;
    // INMP441 outputs 24-bit samples left-justified in a 32-bit I2S frame (it
    // always clocks 32 SCK per WS half-period), so the RX channel must run at
    // 32-bit slot width or the mic reads garbage. STEREO: the INMP441 drives
    // only the left slot (L/R tied low); mic_read keeps the left sample.
    i2s_chan_config_t rx_cc = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)c->port, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_cc, NULL, &s_rx));
    i2s_std_config_t rx_std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = c->sck,
            .ws = c->ws, .dout = I2S_GPIO_UNUSED, .din = c->sd,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &rx_std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
    ESP_LOGI(TAG, "mic ready");
    return ESP_OK;
}

static int mic_read(int16_t *pcm, int samples) {
    if (samples > MIC_MAX_SAMPLES) samples = MIC_MAX_SAMPLES;
    static int32_t raw[MIC_MAX_SAMPLES * 2];   // two 32-bit slots (L,R) per sample
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_rx, raw, samples * 2 * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    if (err != ESP_OK) return -1;
    int frames = (int)(bytes_read / sizeof(int32_t) / 2);
    // Keep the left slot (INMP441 delivers ~18-bit-deep, left-justified). A
    // straight >>16 leaves speech near -60 dBFS; >>11 adds ~+30 dB, clamped.
    for (int i = 0; i < frames; i++) {
        int32_t v = raw[2 * i] >> 11;
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        pcm[i] = (int16_t)v;
    }
    return frames;
}

const mic_ops_t i2s_mic_ops = { .init = mic_init, .read = mic_read };

#endif // SOC_I2S_NUM > 1
