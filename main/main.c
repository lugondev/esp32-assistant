#include "wifi_sta.h"
#include "wifi_cfg.h"
#include "provisioning.h"
#include "display.h"
#include "voice.h"
#include "buttons.h"
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

// Conversation gate: false = idle (mic muted) after connect; the Wake button
// toggles it. mic_task only streams when s_active. Written only by the button
// callback, read by mic_task.
static volatile bool s_active = false;

// jitter buffer: queue of heap-allocated Opus packets
typedef struct { uint8_t data[OPUS_MAX_PACKET]; int len; } pkt_t;
static QueueHandle_t s_pktq;   // holds pkt_t* ; depth ~ 150 ms / 60 ms ≈ a few frames + slack

// Set once in app_main before ws_client_start(); read by on_event() to show
// "host:port" on the session-ready screen without threading cfg through the callback.
// Sized to match wifi_cfg_t.server_host (WIFI_CFG_HOST_MAX=127 + NUL), so a
// long configured hostname isn't silently truncated on the status screen.
static char s_wcfg_host[128];
static int  s_wcfg_port;

// display_show()/voice_play() touch SPI/I2S hardware directly. Calling them
// from inside on_event() — which runs on esp_websocket_client's own internal
// task — crashed reliably right at session-start (Guru Meditation Error /
// LoadProhibited / cache-disabled-access), even after generously bumping
// every stack that task could plausibly be using. Rather than keep guessing
// at the exact mechanism, on_event() only ever queues a status_msg_t here;
// status_task (our own task, own controlled stack) is the only thing that
// ever calls display_show()/voice_play(), same isolation principle as
// mic_task/spk_task already use for the audio hardware.
typedef struct {
    bool play_voice;
    voice_clip_t voice;
    char line1[32];
    char line2[140];
    bool has_line2;
} status_msg_t;
static QueueHandle_t s_status_q;

static void status_task(void *arg) {
    (void)arg;
    status_msg_t m;
    for (;;) {
        if (xQueueReceive(s_status_q, &m, portMAX_DELAY) == pdTRUE) {
            ESP_LOGW(TAG, "status_task: show '%s' voice=%d", m.line1, m.play_voice);  // DIAGNOSTIC
            display_show(m.line1, m.has_line2 ? m.line2 : NULL);
            ESP_LOGW(TAG, "status_task: display done");  // DIAGNOSTIC
            if (m.play_voice) voice_play(m.voice);
            ESP_LOGW(TAG, "status_task: voice done");  // DIAGNOSTIC
        }
    }
}

// Runs in the button task context — only flips flags, adjusts the volume int,
// queues display messages, and sends a WS control frame (network, not
// hardware). It must never call display/audio hardware directly.
static void on_button(button_id_t id) {
    switch (id) {
    case BTN_WAKE: {
        s_active = !s_active;
        // If the bot is mid-speech, a Wake press means "stop / let me talk":
        // tell the gateway to abort the current turn (it replies with
        // "aborted", which flushes the playback queue in on_event).
        if (s_state == APP_SPEAKING) ws_client_send_control("abort");
        status_msg_t m = { .play_voice = false, .has_line2 = true };
        strncpy(m.line1, s_active ? "Listening" : "Idle", sizeof(m.line1) - 1);
        strncpy(m.line2, s_active ? "Speak now" : "Press wake to talk",
                sizeof(m.line2) - 1);
        BaseType_t ok = xQueueSend(s_status_q, &m, 0);  // DIAGNOSTIC ok
        ESP_LOGW(TAG, "on_button WAKE: active=%d queued=%d", s_active, (int)ok);
        break;
    }
    case BTN_VOL_UP:
    case BTN_VOL_DOWN: {
        int v = audio_adjust_volume(id == BTN_VOL_UP ? 10 : -10);
        status_msg_t m = { .play_voice = false, .has_line2 = false };
        snprintf(m.line1, sizeof(m.line1), "Volume %d%%", v);
        xQueueSend(s_status_q, &m, 0);
        break;
    }
    }
}

static void on_event(const wsp_event_t *ev) {
    switch (ev->type) {
    case WSP_EV_SESSION_STARTED: {
        s_state = APP_LISTENING;
        ESP_LOGI(TAG, "session ready");
        // Connected but idle until the user presses Wake (s_active stays false).
        status_msg_t m = { .play_voice = true, .voice = VOICE_CONNECTED, .has_line2 = true };
        strncpy(m.line1, "Connected", sizeof(m.line1) - 1);
        strncpy(m.line2, "Press wake to talk", sizeof(m.line2) - 1);
        xQueueSend(s_status_q, &m, 0);
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
    case WSP_EV_ERROR: {
        ESP_LOGE(TAG, "server error: %s", ev->text);
        status_msg_t m = { .play_voice = false, .has_line2 = true };
        strncpy(m.line1, "Error", sizeof(m.line1) - 1);
        strncpy(m.line2, ev->text, sizeof(m.line2) - 1);
        xQueueSend(s_status_q, &m, 0);
        break;
    }
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

// Mirrors xiaozhi-esp32's architecture: the audio-capture task only encodes
// and queues; a separate uplink_task owns the ws_client_send_audio() call.
// (opus_encode is the stack-heavy part — see mic_task's stack size below.)
typedef struct { uint8_t data[OPUS_MAX_PACKET]; int len; } uplink_pkt_t;
static QueueHandle_t s_uplinkq;

static void mic_task(void *arg) {
    (void)arg;
    int16_t pcm[OPUS_UP_SAMPLES];
    for (;;) {
        int got = audio_mic_read(pcm, OPUS_UP_SAMPLES);   // keeps I2S draining always
        if (got != OPUS_UP_SAMPLES) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        // Only stream when the user has activated the conversation (Wake button),
        // the session is ready, and we're not playing the bot (half-duplex).
        if (!s_active || s_state != APP_LISTENING || !ws_client_connected()) continue;
        uplink_pkt_t *p = malloc(sizeof(uplink_pkt_t));
        if (!p) continue;
        int n = opus_codec_encode(pcm, p->data, sizeof p->data);
        if (n > 0) {
            p->len = n;
            if (xQueueSend(s_uplinkq, &p, 0) != pdTRUE) free(p);   // drop on overflow
        } else {
            free(p);
        }
    }
}

static void uplink_task(void *arg) {
    (void)arg;
    uplink_pkt_t *p;
    for (;;) {
        if (xQueueReceive(s_uplinkq, &p, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        ws_client_send_audio(p->data, p->len);
        free(p);
    }
}

static void spk_task(void *arg) {
    (void)arg;
    int16_t pcm[OPUS_DOWN_SAMPLES_MAX];  // must fit the largest (120ms) Opus frame
                                         // the gateway may send, not just 60ms —
                                         // undersizing this stack-smashed spk_task.
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
    s_uplinkq = xQueueCreate(16, sizeof(uplink_pkt_t *));
    s_status_q = xQueueCreate(4, sizeof(status_msg_t));
    xTaskCreatePinnedToCore(status_task, "status", 8192, NULL, 4, NULL, 1);
    buttons_start(on_button);  // Wake toggles s_active; Vol +/- adjust volume

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

    // mic_task runs opus_encode(), which is extraordinarily stack-hungry on
    // ESP32 (SILK wideband analysis buffers live on the stack): measured
    // ~23KB peak usage via uxTaskGetStackHighWaterMark. 24576 left only ~1.2KB
    // free, so any ISR nesting on top tipped it over into the adjacent heap
    // TCB, corrupting the heap/task lists (crash surfaced elsewhere — WiFi
    // allocs, FreeRTOS tick). 40960 gives a healthy ~17KB margin.
    // spk_task runs opus_decode() (much lighter, ~4.6KB peak); uplink_task
    // owns the ws send chain.
    xTaskCreatePinnedToCore(spk_task, "spk", 16384, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(mic_task, "mic", 40960, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(uplink_task, "uplink", 16384, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "running");
}
