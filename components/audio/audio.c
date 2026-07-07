#include "audio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "audio";
static i2s_chan_handle_t s_rx;  // INMP441 mic, I2S_NUM_0, RX only
static i2s_chan_handle_t s_tx;  // MAX98357A speaker, I2S_NUM_1, TX only
// Serializes ONLY the two speaker writers (spk_task + voice_play) so they don't
// interleave on the TX channel or race on the static scratch buffer below.
// The mic RX channel (I2S_NUM_0) is a separate, independently thread-safe I2S
// channel with a single reader (mic_task), so it must NOT share this lock — if
// mic_task held it across its blocking i2s_channel_read(), the lower-priority
// status_task could never acquire it for voice_play() and would hang forever.
static SemaphoreHandle_t s_tx_mutex;

// Largest samples value any caller passes to audio_mic_read() (mic_task in
// main.c reads OPUS_UP_SAMPLES == 960 at a time). Sized as a fixed buffer
// (not a VLA) to keep stack usage bounded and predictable.
#define AUDIO_MIC_MAX_SAMPLES 960

// Software output volume (0..100). MAX98357A has no hardware volume, so
// audio_spk_write() scales samples by this before the I2S write.
static volatile int s_volume = 80;
#define AUDIO_SPK_SCRATCH 512  // static scratch for volume-scaled chunks

void audio_set_volume(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_volume = pct;
}
int audio_get_volume(void) { return s_volume; }
int audio_adjust_volume(int delta) {
    int v = s_volume + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_volume = v;
    return v;
}

esp_err_t audio_init(void) {
    // Mic: INMP441 outputs 24-bit samples left-justified in a 32-bit I2S
    // frame (it always clocks 32 SCK cycles per WS half-period, regardless
    // of what bit width you ask for) — the RX channel must run at 32-bit
    // slot width or the mic reads garbage/silence. audio_mic_read() shifts
    // each 32-bit frame down to 16-bit PCM.
    i2s_chan_config_t rx_cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_cc, NULL, &s_rx));
    i2s_std_config_t rx_std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = CONFIG_AA_MIC_SCK,
            .ws = CONFIG_AA_MIC_WS, .dout = I2S_GPIO_UNUSED,
            .din = CONFIG_AA_MIC_SD,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &rx_std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));

    // Speaker: MAX98357A takes standard 16-bit I2S directly, no bit-shift
    // conversion needed on the way out.
    i2s_chan_config_t tx_cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_cc, &s_tx, NULL));
    i2s_std_config_t tx_std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = CONFIG_AA_SPK_BCLK,
            .ws = CONFIG_AA_SPK_LRC, .dout = CONFIG_AA_SPK_DIN,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &tx_std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));

    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) { ESP_LOGE(TAG, "mutex create failed"); return ESP_FAIL; }

    ESP_LOGI(TAG, "audio ready");
    return ESP_OK;
}

int audio_mic_read(int16_t *pcm, int samples) {
    if (samples > AUDIO_MIC_MAX_SAMPLES) samples = AUDIO_MIC_MAX_SAMPLES;
    static int32_t raw[AUDIO_MIC_MAX_SAMPLES];
    size_t bytes_read = 0;

    // No lock: s_rx is a dedicated I2S channel read only by mic_task.
    esp_err_t err = i2s_channel_read(s_rx, raw, samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    if (err != ESP_OK) return -1;

    int got = (int)(bytes_read / sizeof(int32_t));
    for (int i = 0; i < got; i++) pcm[i] = (int16_t)(raw[i] >> 16);
    return got;
}

int audio_spk_write(const int16_t *pcm, int samples) {
    int vol = s_volume;
    size_t total_written = 0;
    esp_err_t err = ESP_OK;

    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    if (vol >= 100) {
        // Passthrough — no scaling needed.
        err = i2s_channel_write(s_tx, pcm, samples * sizeof(int16_t),
                                &total_written, portMAX_DELAY);
    } else {
        // Scale in chunks through a static scratch buffer (input is const).
        static int16_t scratch[AUDIO_SPK_SCRATCH];
        int off = 0;
        while (off < samples) {
            int chunk = samples - off;
            if (chunk > AUDIO_SPK_SCRATCH) chunk = AUDIO_SPK_SCRATCH;
            for (int i = 0; i < chunk; i++)
                scratch[i] = (int16_t)(((int32_t)pcm[off + i] * vol) / 100);
            size_t bw = 0;
            err = i2s_channel_write(s_tx, scratch, chunk * sizeof(int16_t),
                                    &bw, portMAX_DELAY);
            total_written += bw;
            if (err != ESP_OK) break;
            off += chunk;
        }
    }
    xSemaphoreGive(s_tx_mutex);
    if (err != ESP_OK) return -1;
    return (int)(total_written / sizeof(int16_t));
}
