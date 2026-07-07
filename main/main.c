#include "wifi_sta.h"
#include "wifi_cfg.h"
#include "provisioning.h"
#include "display.h"
#include "voice.h"
#include "nvs_flash.h"
#include "ws_client.h"
#include "audio.h"
#include "opus_codec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Bool Kconfig options are undefined (not 0) when unset — provide a fallback.
#ifndef CONFIG_AA_SERVER_SECURE
#define CONFIG_AA_SERVER_SECURE 0
#endif

static const char *TAG = "app";

typedef enum { APP_CONNECTING, APP_LISTENING, APP_SPEAKING } app_state_t;
// Single-writer: only the ws event callback writes s_state; mic_task/spk_task read it.
static volatile app_state_t s_state = APP_CONNECTING;

// jitter buffer: queue of heap-allocated Opus packets
typedef struct { uint8_t data[OPUS_MAX_PACKET]; int len; } pkt_t;
static QueueHandle_t s_pktq;   // holds pkt_t* ; depth ~ 150 ms / 60 ms ≈ a few frames + slack

// Set once in app_main before ws_client_start(); read by on_event() to show
// "host:port" on the session-ready screen without threading cfg through the callback.
// Sized to match wifi_cfg_t.server_host (WIFI_CFG_HOST_MAX=127 + NUL), so a
// long configured hostname isn't silently truncated on the status screen.
static char s_wcfg_host[128];
static int  s_wcfg_port;

static void on_event(const wsp_event_t *ev) {
    switch (ev->type) {
    case WSP_EV_SESSION_STARTED: {
        s_state = APP_LISTENING;
        ESP_LOGI(TAG, "session ready");
        char host_port[128 + 1 + 6];  // host + ':' + up to 5-digit port + NUL
        snprintf(host_port, sizeof host_port, "%s:%d", s_wcfg_host, s_wcfg_port);
        display_show("Connected", host_port);
        voice_play(VOICE_CONNECTED);
        break;
    }
    case WSP_EV_USER_TRANSCRIPT: ESP_LOGI(TAG, "you: %s", ev->text); break;
    case WSP_EV_RESPONSE_TEXT:   ESP_LOGI(TAG, "bot: %s", ev->text); break;
    case WSP_EV_AUDIO_START:     s_state = APP_SPEAKING; break;
    case WSP_EV_TURN_DONE:       s_state = APP_LISTENING; break;
    case WSP_EV_ABORTED:
        s_state = APP_LISTENING;
        { pkt_t *p; while (xQueueReceive(s_pktq, &p, 0) == pdTRUE) free(p); }  // flush
        break;
    case WSP_EV_ERROR:
        ESP_LOGE(TAG, "server error: %s", ev->text);
        display_show("Error", ev->text);
        break;
    default: break;
    }
}

static void on_audio(const uint8_t *data, int len) {
    if (len <= 0 || len > OPUS_MAX_PACKET) return;
    pkt_t *p = malloc(sizeof(pkt_t));
    if (!p) return;
    memcpy(p->data, data, len); p->len = len;
    if (xQueueSend(s_pktq, &p, 0) != pdTRUE) free(p);   // drop on overflow
}

static void mic_task(void *arg) {
    (void)arg;
    int16_t pcm[OPUS_UP_SAMPLES];
    uint8_t opus[OPUS_MAX_PACKET];
    for (;;) {
        int got = audio_mic_read(pcm, OPUS_UP_SAMPLES);   // keeps I2S draining always
        if (got != OPUS_UP_SAMPLES) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (s_state != APP_LISTENING || !ws_client_connected()) continue;  // half-duplex
        int n = opus_codec_encode(pcm, opus, sizeof opus);
        if (n > 0) ws_client_send_audio(opus, n);
    }
}

static void spk_task(void *arg) {
    (void)arg;
    int16_t pcm[OPUS_DOWN_SAMPLES];
    pkt_t *p;
    for (;;) {
        if (xQueueReceive(s_pktq, &p, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        int n = opus_codec_decode(p->data, p->len, pcm);
        free(p);
        if (n > 0) audio_spk_write(pcm, n);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "esp32-assistant booting");
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(audio_init());  // moved earlier: voice_play() needs the codec
                                     // ready before the first status announcement,
                                     // and audio_init() has no WiFi dependency.

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    wifi_cfg_t cfg;
    ESP_ERROR_CHECK(wifi_cfg_load(&cfg));

    display_show("Connecting WiFi...", NULL);
    voice_play(VOICE_CONNECTING);
    ESP_ERROR_CHECK(wifi_sta_start(cfg.ssid, cfg.password));
    if (!wifi_sta_wait_connected(15000)) {
        ESP_LOGW(TAG, "wifi connect failed, starting provisioning portal");
        display_show("WiFi failed", "Starting setup AP...");
        provisioning_start(&cfg);  // does not return
    }

    display_show("WiFi OK", "Connecting gateway...");
    ESP_ERROR_CHECK(opus_codec_init());

    s_pktq = xQueueCreate(16, sizeof(pkt_t *));   // ~16*60ms buffer ceiling

    wsp_config_t wcfg = {
        .host = cfg.server_host, .port = cfg.server_port,
        .secure = CONFIG_AA_SERVER_SECURE,
        .stt_engine = CONFIG_AA_STT_ENGINE, .tts_engine = CONFIG_AA_TTS_ENGINE,
        .language = CONFIG_AA_LANGUAGE, .sample_rate = 16000, .output_sample_rate = 16000,
        .profile = CONFIG_AA_PROFILE,
    };
    strncpy(s_wcfg_host, cfg.server_host, sizeof(s_wcfg_host) - 1);
    s_wcfg_port = cfg.server_port;
    ESP_ERROR_CHECK(ws_client_start(&wcfg, on_event, on_audio));

    // 4096 was enough for the old esp_codec_dev-based audio path; the raw
    // i2s_channel_read()/i2s_channel_write() driver calls (added when audio.c
    // was rewritten for the INMP441/MAX98357A hardware) use more stack and
    // overflowed mic_task's — bumped both tasks generously.
    xTaskCreatePinnedToCore(spk_task, "spk", 8192, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(mic_task, "mic", 8192, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "running");
}
