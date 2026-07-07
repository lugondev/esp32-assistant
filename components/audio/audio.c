#include "audio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "audio";
static i2s_chan_handle_t s_rx;  // INMP441 mic, I2S_NUM_0, RX only
static i2s_chan_handle_t s_tx;  // MAX98357A speaker, I2S_NUM_1, TX only
static SemaphoreHandle_t s_mutex;

// Largest samples value any caller passes to audio_mic_read() (mic_task in
// main.c reads OPUS_UP_SAMPLES == 960 at a time). Sized as a fixed buffer
// (not a VLA) to keep stack usage bounded and predictable.
#define AUDIO_MIC_MAX_SAMPLES 960

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

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) { ESP_LOGE(TAG, "mutex create failed"); return ESP_FAIL; }

    ESP_LOGI(TAG, "audio ready");
    return ESP_OK;
}

int audio_mic_read(int16_t *pcm, int samples) {
    if (samples > AUDIO_MIC_MAX_SAMPLES) samples = AUDIO_MIC_MAX_SAMPLES;
    static int32_t raw[AUDIO_MIC_MAX_SAMPLES];
    size_t bytes_read = 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = i2s_channel_read(s_rx, raw, samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    xSemaphoreGive(s_mutex);
    if (err != ESP_OK) return -1;

    int got = (int)(bytes_read / sizeof(int32_t));
    for (int i = 0; i < got; i++) pcm[i] = (int16_t)(raw[i] >> 16);
    return got;
}

int audio_spk_write(const int16_t *pcm, int samples) {
    size_t bytes_written = 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = i2s_channel_write(s_tx, pcm, samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    xSemaphoreGive(s_mutex);
    if (err != ESP_OK) return -1;
    return (int)(bytes_written / sizeof(int16_t));
}
