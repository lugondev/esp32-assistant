#include "i2s_fd.h"
#include "driver/i2s_std.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if SOC_I2S_NUM == 1   // single-I2S SoCs (e.g. ESP32-C3): mic+speaker share one controller

static const char *TAG = "i2s_fd";
static i2s_chan_handle_t s_rx, s_tx;
static SemaphoreHandle_t s_tx_mutex;
static volatile int s_volume = 80;
static bool s_ready;   // guards the shared full-duplex init (both ops call it)

#define FD_MIC_MAX_SAMPLES 960
#define FD_SPK_SCRATCH 512

// Allocate the shared full-duplex controller once. Idempotent: mic->init and
// speaker->init both call it; init order does not matter.
// C3 full-duplex — UNVERIFIED on hardware.
static esp_err_t fd_ensure_init(const void *cfg_v) {
    if (s_ready) return ESP_OK;
    const i2s_fd_cfg_t *c = (const i2s_fd_cfg_t *)cfg_v;
    // RX+TX share BCLK+WS, so both use ONE uniform slot config. The INMP441
    // needs 32-bit slots, so RX and TX are both 32-bit STEREO @16kHz (differing
    // bit widths cannot share one BCLK). C3 full-duplex — UNVERIFIED on hardware.
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &s_tx, &s_rx));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = c->bclk, .ws = c->ws,
            .dout = c->spk_data, .din = c->mic_data,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &std));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) { ESP_LOGE(TAG, "mutex create failed"); return ESP_FAIL; }
    s_ready = true;
    ESP_LOGI(TAG, "i2s full-duplex ready (UNVERIFIED on hardware)");
    return ESP_OK;
}

static esp_err_t fd_mic_init(const void *cfg) { return fd_ensure_init(cfg); }
static esp_err_t fd_spk_init(const void *cfg) { return fd_ensure_init(cfg); }

static int fd_mic_read(int16_t *pcm, int samples) {
    if (samples > FD_MIC_MAX_SAMPLES) samples = FD_MIC_MAX_SAMPLES;
    static int32_t raw[FD_MIC_MAX_SAMPLES * 2];   // L,R 32-bit slots
    size_t br = 0;
    if (i2s_channel_read(s_rx, raw, samples * 2 * sizeof(int32_t), &br, portMAX_DELAY) != ESP_OK)
        return -1;
    int frames = (int)(br / sizeof(int32_t) / 2);
    for (int i = 0; i < frames; i++) {            // keep left slot, +30dB, clamp
        int32_t v = raw[2 * i] >> 11;
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        pcm[i] = (int16_t)v;
    }
    return frames;
}

static void fd_set_volume(int pct) { if (pct < 0) pct = 0; if (pct > 100) pct = 100; s_volume = pct; }
static int  fd_get_volume(void) { return s_volume; }
static int  fd_adjust_volume(int d) { int v = s_volume + d; if (v < 0) v = 0; if (v > 100) v = 100; s_volume = v; return v; }

static int fd_spk_write(const int16_t *pcm, int samples) {
    // MAX98357A on a 32-bit STEREO frame: pack each 16-bit mono sample into the
    // top 16 bits of both L and R slots (volume-scaled first).
    // C3 full-duplex — UNVERIFIED on hardware.
    int vol = s_volume;
    static int32_t frame[FD_SPK_SCRATCH * 2];   // 2 slots per sample
    size_t written_frames = 0;
    esp_err_t err = ESP_OK;
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    int off = 0;
    while (off < samples) {
        int chunk = samples - off;
        if (chunk > FD_SPK_SCRATCH) chunk = FD_SPK_SCRATCH;
        for (int i = 0; i < chunk; i++) {
            int32_t s = pcm[off + i];
            if (vol < 100) s = (s * vol) / 100;
            int32_t slot = (int32_t)((uint32_t)s << 16);   // 16-bit PCM into 32-bit slot MSBs (unsigned shift avoids signed-shift UB)
            frame[2 * i] = slot; frame[2 * i + 1] = slot;   // duplicate L,R
        }
        size_t bw = 0;
        err = i2s_channel_write(s_tx, frame, chunk * 2 * sizeof(int32_t), &bw, portMAX_DELAY);
        written_frames += bw / (2 * sizeof(int32_t));
        if (err != ESP_OK) break;
        off += chunk;
    }
    xSemaphoreGive(s_tx_mutex);
    if (err != ESP_OK) return -1;
    return (int)written_frames;   // samples written
}

static void fd_spk_reset(void) {
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    i2s_channel_disable(s_tx);
    i2s_channel_enable(s_tx);
    xSemaphoreGive(s_tx_mutex);
}

const mic_ops_t i2s_fd_mic_ops = { .init = fd_mic_init, .read = fd_mic_read };
const speaker_ops_t i2s_fd_speaker_ops = {
    .init = fd_spk_init, .write = fd_spk_write, .reset = fd_spk_reset,
    .set_volume = fd_set_volume, .get_volume = fd_get_volume, .adjust_volume = fd_adjust_volume,
};

#endif // SOC_I2S_NUM == 1
