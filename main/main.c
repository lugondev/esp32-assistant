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
#include "esp_timer.h"
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
// s_state gates the half-duplex mic (mic_task streams only in APP_LISTENING). Writers:
// the ws event callback sets APP_SPEAKING / APP_CONNECTING / (APP_LISTENING on abort or
// a text-only turn); spk_task sets APP_LISTENING once playback has fully DRAINED. This
// hand-off matters: TURN_DONE means the server finished *sending*, but the jitter buffer
// may still hold hundreds of ms of the bot's voice. Reopening the mic at TURN_DONE lets
// it capture that trailing audio and transcribe it as user speech -> a self-talk loop.
// So TURN_DONE only arms s_turn_ending; spk_task flips to LISTENING after the buffer
// empties. The two writers touch disjoint transitions, so no lock is needed.
static volatile app_state_t s_state = APP_CONNECTING;
static volatile bool s_turn_ending = false;  // TURN_DONE seen; waiting for playback to drain
// True while status_task plays a local voice clip (e.g. the "connected / sẵn sàng"
// announcement) out the speaker. Those clips bypass the APP_SPEAKING state machine, so
// without this the mic would capture them and transcribe the announcement as user speech
// (observed: "sẵn sàng" prepended to what the user actually said). Single-writer:
// status_task sets/clears it; mic_task reads it.
static volatile bool s_voice_busy = false;

// Conversation gate: false = idle (mic muted) after connect; the Wake button
// toggles it. mic_task only streams when s_active. Written only by the button
// callback, read by mic_task.
static volatile bool s_active = false;

// Idle: server `goodbye` is primary; this device-side watchdog is a backup for a
// silently dropped WS (no goodbye arrives). idle_timeout_s comes from `welcome`.
static volatile int     s_idle_timeout_s = 0;   // 0 = no device-side timeout
static volatile int64_t s_last_activity_us = 0;

// jitter buffer: queue of heap-allocated Opus packets
typedef struct { uint8_t data[OPUS_MAX_PACKET]; int len; } pkt_t;
static QueueHandle_t s_pktq;   // holds pkt_t* ; depth ~ 150 ms / 60 ms ≈ a few frames + slack

// Prime the jitter buffer before starting playback so a reply doesn't underrun
// between sentence chunks. The gateway bursts each chunk's Opus frames then pauses
// to synthesize the next chunk; without slack the queue can hit empty mid-reply and
// spk_task stalls -> an audible gap ("giật cục"). We hold playback until this many
// frames (~4 * 60 ms = 240 ms slack) are buffered, then drain, and re-prime whenever
// the queue empties. Lower it to shave first-audio latency; raise it if replies still
// stutter. A short reply that never reaches this depth still plays once the turn ends
// (s_turn_ending is armed), so it can never get stuck priming.
#define SPK_PREBUFFER_FRAMES 4

// After the reply's audio has drained, wait this long before reopening the mic so the
// final I2S DMA buffer plays out and the room echo decays — otherwise the mic captures
// the tail of our own speech and the STT transcribes it, looping the bot into talking
// to itself. Raise it if self-talk still happens; lower it to reply-to-speech faster.
#define SPK_TAIL_GUARD_MS 250

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
            display_show(m.line1, m.has_line2 ? m.line2 : NULL);
            if (m.play_voice) {
                // Mute the mic for the clip's duration + tail so the announcement
                // doesn't echo into STT (voice_play blocks until the clip is written).
                s_voice_busy = true;
                voice_play(m.voice);
                vTaskDelay(pdMS_TO_TICKS(SPK_TAIL_GUARD_MS));
                s_voice_busy = false;
            }
        }
    }
}

// Runs in the button task context — only flips flags, adjusts the volume int,
// queues display messages, and sends a WS control frame (network, not
// hardware). It must never call display/audio hardware directly.
static void on_button(button_id_t id) {
    switch (id) {
    case BTN_WAKE: {
        if (s_state == APP_SPEAKING) {
            // Barge-in: stop the bot NOW, locally, before the network round-trip.
            // Flush queued packets, drop the committed I2S DMA, reset the decoder
            // so the next turn starts clean (no click/warble). Then tell the server
            // to cancel the turn. Connection stays open; go straight to LISTENING so
            // the user can speak — do NOT toggle to Idle.
            { pkt_t *p; while (xQueueReceive(s_pktq, &p, 0) == pdTRUE) free(p); }
            audio_spk_reset();
            opus_codec_reset();
            s_turn_ending = false;
            s_state = APP_LISTENING;
            s_active = true;
            s_last_activity_us = esp_timer_get_time();
            ws_client_send_abort("user");
            status_msg_t m = { .play_voice = false, .has_line2 = true };
            strncpy(m.line1, "Listening", sizeof(m.line1) - 1);
            strncpy(m.line2, "Speak now", sizeof(m.line2) - 1);
            xQueueSend(s_status_q, &m, 0);
            break;
        }
        // Not speaking: toggle idle/listening.
        s_active = !s_active;
        s_last_activity_us = esp_timer_get_time();
        status_msg_t m = { .play_voice = false, .has_line2 = true };
        strncpy(m.line1, s_active ? "Listening" : "Idle", sizeof(m.line1) - 1);
        strncpy(m.line2, s_active ? "Speak now" : "Press wake to talk",
                sizeof(m.line2) - 1);
        xQueueSend(s_status_q, &m, 0);
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

static void on_event(const lugo_event_t *ev) {
    s_last_activity_us = esp_timer_get_time();  // any server event = activity
    switch (ev->type) {
    case LUGO_EV_WELCOME: {
        s_state = APP_LISTENING;
        if (ev->idle_timeout_s > 0) s_idle_timeout_s = ev->idle_timeout_s;
        ESP_LOGI(TAG, "session ready (idle_timeout_s=%d)", ev->idle_timeout_s);
        // Connected but idle until the user presses Wake (s_active stays false).
        status_msg_t m = { .play_voice = true, .voice = VOICE_CONNECTED, .has_line2 = true };
        strncpy(m.line1, "Connected", sizeof(m.line1) - 1);
        strncpy(m.line2, "Press wake to talk", sizeof(m.line2) - 1);
        xQueueSend(s_status_q, &m, 0);
        break;
    }
    case LUGO_EV_STT:          ESP_LOGI(TAG, "you: %s", ev->text); break;
    case LUGO_EV_TTS_SENTENCE: ESP_LOGI(TAG, "bot: %s", ev->text); break;
    case LUGO_EV_TTS_START:    s_turn_ending = false; s_state = APP_SPEAKING; break;
    case LUGO_EV_TTS_STOP:
        // Turn ended (natural end OR server-side abort — both map to tts stop).
        // Don't open the mic yet: the jitter buffer may still be playing. Arm the
        // drain hand-off; spk_task returns us to LISTENING once empty. If nothing
        // is playing (text-only turn / already stopped by local barge-in), switch now.
        if (s_state == APP_SPEAKING) s_turn_ending = true;
        else s_state = APP_LISTENING;
        break;
    case LUGO_EV_GOODBYE: {
        // Server idle disconnect. Go idle (mic muted); user presses Wake to talk
        // again. The WS may auto-reconnect underneath (Phase 1); that's harmless.
        s_active = false;
        s_turn_ending = false;
        s_state = APP_LISTENING;
        { pkt_t *p; while (xQueueReceive(s_pktq, &p, 0) == pdTRUE) free(p); }  // flush
        status_msg_t m = { .play_voice = false, .has_line2 = true };
        strncpy(m.line1, "Idle", sizeof(m.line1) - 1);
        strncpy(m.line2, "Press wake to talk", sizeof(m.line2) - 1);
        xQueueSend(s_status_q, &m, 0);
        break;
    }
    case LUGO_EV_ERROR: {
        ESP_LOGE(TAG, "server error: %s", ev->text);
        status_msg_t m = { .play_voice = false, .has_line2 = true };
        strncpy(m.line1, "Error", sizeof(m.line1) - 1);
        strncpy(m.line2, ev->text, sizeof(m.line2) - 1);
        xQueueSend(s_status_q, &m, 0);
        break;
    }
    default: break;  // LUGO_EV_MCP / LUGO_EV_UNKNOWN
    }
}

static void on_audio(const uint8_t *data, int len) {
    s_last_activity_us = esp_timer_get_time();
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
        if (!s_active || s_state != APP_LISTENING || s_voice_busy || !ws_client_connected()) continue;
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
    bool priming = true;   // build slack before the first frame of each burst
    for (;;) {
        if (priming) {
            // Wait for buffer slack before draining. Exceptions that start playback
            // early: enough frames buffered (q >= prime depth), or the turn is ending
            // (s_turn_ending) so the buffered tail must flush even if short. q==0 just
            // keeps us idle here. Keyed on s_turn_ending, not s_state, because the turn
            // now stays APP_SPEAKING until this task drains it.
            UBaseType_t q = uxQueueMessagesWaiting(s_pktq);
            if (q == 0 || (q < SPK_PREBUFFER_FRAMES && !s_turn_ending)) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            priming = false;
        }
        if (xQueueReceive(s_pktq, &p, pdMS_TO_TICKS(100)) != pdTRUE) {
            priming = true;   // queue drained — re-prime before the next burst
            if (s_turn_ending) {
                // Playback of the reply has fully drained. Let the last I2S DMA buffer
                // finish and the room echo decay before reopening the mic, so we don't
                // transcribe our own trailing audio (the self-talk loop). THEN listen.
                vTaskDelay(pdMS_TO_TICKS(SPK_TAIL_GUARD_MS));
                s_turn_ending = false;
                s_state = APP_LISTENING;
            }
            continue;
        }
        int n = opus_codec_decode(p->data, p->len, pcm);
        free(p);
        if (n > 0) audio_spk_write(pcm, n);
    }
}

// Backup for a silently dropped WS where the server's `goodbye` never arrives:
// if the user is active but nothing has happened for idle_timeout_s + grace, go
// idle locally. The server remains the primary idle authority.
static void idle_watchdog_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int to = s_idle_timeout_s;
        if (!s_active || to <= 0) continue;
        int64_t idle_us = esp_timer_get_time() - s_last_activity_us;
        if (idle_us >= (int64_t)(to + 5) * 1000000LL) {
            s_active = false;
            status_msg_t m = { .play_voice = false, .has_line2 = true };
            strncpy(m.line1, "Idle", sizeof(m.line1) - 1);
            strncpy(m.line2, "Press wake to talk", sizeof(m.line2) - 1);
            xQueueSend(s_status_q, &m, 0);
        }
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
    xTaskCreate(idle_watchdog_task, "idle_wd", 3072, NULL, 3, NULL);

    // STT/TTS/language all come from the chatllm profile server-side; the device
    // configures only which profile to connect to (CONFIG_AA_PROFILE). Downlink
    // is decoded at 16 kHz to match the device opus decoder.
    s_last_activity_us = esp_timer_get_time();
    ESP_ERROR_CHECK(ws_client_start(
        cfg.server_host, cfg.server_port, CONFIG_AA_SERVER_SECURE,
        CONFIG_AA_PROFILE, 16000, 16000, 60, on_event, on_audio));

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
