#include "wifi_sta.h"
#include "ws_client.h"
#include "audio.h"
#include "opus_codec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "app";

typedef enum { APP_CONNECTING, APP_LISTENING, APP_SPEAKING } app_state_t;
// Single-writer: only the ws event callback writes s_state; mic_task/spk_task read it.
static volatile app_state_t s_state = APP_CONNECTING;

// jitter buffer: queue of heap-allocated Opus packets
typedef struct { uint8_t data[OPUS_MAX_PACKET]; int len; } pkt_t;
static QueueHandle_t s_pktq;   // holds pkt_t* ; depth ~ 150 ms / 60 ms ≈ a few frames + slack

static void on_event(const wsp_event_t *ev) {
    switch (ev->type) {
    case WSP_EV_SESSION_STARTED: s_state = APP_LISTENING; ESP_LOGI(TAG, "session ready"); break;
    case WSP_EV_USER_TRANSCRIPT: ESP_LOGI(TAG, "you: %s", ev->text); break;
    case WSP_EV_RESPONSE_TEXT:   ESP_LOGI(TAG, "bot: %s", ev->text); break;
    case WSP_EV_AUDIO_START:     s_state = APP_SPEAKING; break;
    case WSP_EV_TURN_DONE:       s_state = APP_LISTENING; break;
    case WSP_EV_ABORTED:
        s_state = APP_LISTENING;
        { pkt_t *p; while (xQueueReceive(s_pktq, &p, 0) == pdTRUE) free(p); }  // flush
        break;
    case WSP_EV_ERROR:           ESP_LOGE(TAG, "server error: %s", ev->text); break;
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
        if (got != OPUS_UP_SAMPLES) continue;
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
    ESP_ERROR_CHECK(wifi_sta_start());
    if (!wifi_sta_wait_connected(20000)) { ESP_LOGE(TAG, "wifi timeout"); return; }
    ESP_ERROR_CHECK(audio_init());
    ESP_ERROR_CHECK(opus_codec_init());

    s_pktq = xQueueCreate(16, sizeof(pkt_t *));   // ~16*60ms buffer ceiling

    wsp_config_t cfg = {
        .host = CONFIG_AA_SERVER_HOST, .port = CONFIG_AA_SERVER_PORT,
        .secure = CONFIG_AA_SERVER_SECURE,
        .stt_engine = CONFIG_AA_STT_ENGINE, .tts_engine = CONFIG_AA_TTS_ENGINE,
        .language = CONFIG_AA_LANGUAGE, .sample_rate = 16000, .output_sample_rate = 24000,
    };
    ESP_ERROR_CHECK(ws_client_start(&cfg, on_event, on_audio));

    xTaskCreatePinnedToCore(spk_task, "spk", 4096, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(mic_task, "mic", 4096, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "running");
}
