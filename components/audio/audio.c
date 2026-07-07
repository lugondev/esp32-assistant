#include "audio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx, s_rx;
static esp_codec_dev_handle_t s_dev;
static SemaphoreHandle_t s_mutex;

static esp_err_t init_i2s(void) {
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &s_tx, &s_rx));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = CONFIG_AA_I2S_MCLK, .bclk = CONFIG_AA_I2S_BCLK,
            .ws = CONFIG_AA_I2S_WS, .dout = CONFIG_AA_I2S_DOUT,
            .din = CONFIG_AA_I2S_DIN,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
    return ESP_OK;
}

esp_err_t audio_init(void) {
    i2c_master_bus_handle_t i2c;
    i2c_master_bus_config_t ic = {
        .i2c_port = I2C_NUM_0, .sda_io_num = CONFIG_AA_I2C_SDA,
        .scl_io_num = CONFIG_AA_I2C_SCL, .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&ic, &i2c));
    ESP_ERROR_CHECK(init_i2s());

    audio_codec_i2s_cfg_t di = { .port = I2S_NUM_0, .rx_handle = s_rx, .tx_handle = s_tx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&di);
    audio_codec_i2c_cfg_t ci = { .port = I2C_NUM_0, .addr = CONFIG_AA_ES8311_ADDR, .bus_handle = i2c };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&ci);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!data_if || !ctrl_if || !gpio_if) {
        ESP_LOGE(TAG, "codec interface alloc failed");
        return ESP_FAIL;
    }

    es8311_codec_cfg_t es = {
        .ctrl_if = ctrl_if, .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = -1, .use_mclk = true,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es);
    if (!codec_if) { ESP_LOGE(TAG, "es8311_codec_new failed"); return ESP_FAIL; }
    esp_codec_dev_cfg_t dc = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = codec_if, .data_if = data_if };
    s_dev = esp_codec_dev_new(&dc);
    if (!s_dev) { ESP_LOGE(TAG, "esp_codec_dev_new failed"); return ESP_FAIL; }

    // Single 16 kHz mono format for both capture and playback (half-duplex).
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 1, .sample_rate = 16000 };
    ESP_ERROR_CHECK(esp_codec_dev_open(s_dev, &fs));
    esp_codec_dev_set_out_vol(s_dev, 80);
    esp_codec_dev_set_in_gain(s_dev, 30.0);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) { ESP_LOGE(TAG, "mutex create failed"); return ESP_FAIL; }

    ESP_LOGI(TAG, "audio ready");
    return ESP_OK;
}

int audio_mic_read(int16_t *pcm, int samples) {
    int bytes = samples * (int)sizeof(int16_t);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int ret = esp_codec_dev_read(s_dev, pcm, bytes) == ESP_CODEC_DEV_OK ? samples : -1;
    xSemaphoreGive(s_mutex);
    return ret;
}

int audio_spk_write(const int16_t *pcm, int samples) {
    int bytes = samples * (int)sizeof(int16_t);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int ret = esp_codec_dev_write(s_dev, (void *)pcm, bytes) == ESP_CODEC_DEV_OK ? samples : -1;
    xSemaphoreGive(s_mutex);
    return ret;
}
