#include "i2s_speaker.h"
#include "driver/i2s_std.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if SOC_I2S_NUM > 1   // dedicated TX controller (dual-I2S SoCs, e.g. ESP32-S3)

static const char *TAG = "i2s_speaker";
static i2s_chan_handle_t s_tx;
// Serializes the two speaker writers (spk_task + voice_play) so they don't
// interleave on the TX channel or race on the static scratch buffer below.
static SemaphoreHandle_t s_tx_mutex;

// Software output volume (0..100). MAX98357A has no hardware volume, so
// spk_write() scales samples by this before the I2S write.
static volatile int s_volume = 80;
#define SPK_SCRATCH 512

static void spk_set_volume(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_volume = pct;
}
static int spk_get_volume(void) { return s_volume; }
static int spk_adjust_volume(int delta) {
    int v = s_volume + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_volume = v;
    return v;
}

static esp_err_t spk_init(const void *cfg_v) {
    const i2s_speaker_cfg_t *c = (const i2s_speaker_cfg_t *)cfg_v;
    // MAX98357A takes standard 16-bit I2S directly, no bit-shift on the way out.
    i2s_chan_config_t tx_cc = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)c->port, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_cc, &s_tx, NULL));
    i2s_std_config_t tx_std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = c->bclk,
            .ws = c->lrc, .dout = c->din, .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &tx_std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));

    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) { ESP_LOGE(TAG, "mutex create failed"); return ESP_FAIL; }
    ESP_LOGI(TAG, "speaker ready");
    return ESP_OK;
}

static int spk_write(const int16_t *pcm, int samples) {
    int vol = s_volume;
    size_t total_written = 0;
    esp_err_t err = ESP_OK;
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    if (vol >= 100) {
        err = i2s_channel_write(s_tx, pcm, samples * sizeof(int16_t),
                                &total_written, portMAX_DELAY);
    } else {
        static int16_t scratch[SPK_SCRATCH];
        int off = 0;
        while (off < samples) {
            int chunk = samples - off;
            if (chunk > SPK_SCRATCH) chunk = SPK_SCRATCH;
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

static void spk_reset(void) {
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    // Disable+re-enable the TX channel to discard queued DMA (barge-in).
    i2s_channel_disable(s_tx);
    i2s_channel_enable(s_tx);
    xSemaphoreGive(s_tx_mutex);
}

const speaker_ops_t i2s_speaker_ops = {
    .init = spk_init, .write = spk_write, .reset = spk_reset,
    .set_volume = spk_set_volume, .get_volume = spk_get_volume, .adjust_volume = spk_adjust_volume,
};

#endif // SOC_I2S_NUM > 1
