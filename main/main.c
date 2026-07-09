#include "wifi_sta.h"
#include "wifi_cfg.h"
#include "provisioning.h"
#include "display.h"
#include "robot_eyes.h"
#include "gfx.h"
#include "voice.h"
#include "buttons.h"
#include "board.h"
#include "nvs_flash.h"
#include "ws_client.h"
#include "audio.h"
#include "opus_codec.h"
#include "mcp_tools.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ESP32-C3 is single-core: core 1 does not exist. Pin audio tasks to core 1 on
// dual-core (S3, keeping them off core 0 where WiFi runs); let the scheduler
// place them on unicore targets.
#if CONFIG_FREERTOS_UNICORE
#define APP_CPU_AUDIO tskNO_AFFINITY
#else
#define APP_CPU_AUDIO 1
#endif

// Bool Kconfig options are undefined (not 0) when unset — provide a fallback.
#ifndef CONFIG_AA_SERVER_SECURE
#define CONFIG_AA_SERVER_SECURE 0
#endif

static const char *TAG = "app";

typedef enum { APP_CONNECTING, APP_LISTENING, APP_SPEAKING } app_state_t;
// s_state gates the half-duplex mic (mic_task streams only in APP_LISTENING) and
// whether downlink audio is accepted (on_audio drops unless APP_SPEAKING).
// Writers: the ws event callback (WELCOME/TTS_START/TTS_STOP/GOODBYE), the button
// task (barge-in -> APP_LISTENING), and spk_task (-> APP_LISTENING after the jitter
// buffer drains). All concurrent writers either set APP_LISTENING or the ws
// callback drives the SPEAKING/LISTENING turn edges; the writes are single-word
// and idempotent per transition, so no lock is needed, but keep new writers to
// this same discipline.
// This hand-off matters: TURN_DONE means the server finished *sending*, but the
// jitter buffer may still hold hundreds of ms of the bot's voice. Reopening the
// mic at TURN_DONE lets it capture that trailing audio and transcribe it as user
// speech -> a self-talk loop. So TURN_DONE only arms s_turn_ending; spk_task
// flips to LISTENING after the buffer empties.
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

// Barge-in request: set by the button task (which must not touch audio hardware
// from its small stack — cross-task I2S/SPI calls have crashed here before), and
// serviced by spk_task (the owner of the I2S TX + opus decoder), which flushes
// the jitter queue, drops the DMA, and resets the decoder. One-shot flag.
static volatile bool s_barge_in = false;

// Idle: server `goodbye` is primary; this device-side watchdog is a backup for a
// silently dropped WS (no goodbye arrives). idle_timeout_s comes from `welcome`.
static volatile int      s_idle_timeout_s = 0;   // 0 = no device-side timeout
// Seconds (not a 64-bit microsecond counter): a 64-bit value written by the ws
// task and read by idle_watchdog_task isn't atomic on the 32-bit Xtensa core,
// so a torn read could suppress or spuriously trip the watchdog. uint32_t
// load/store is atomic here.
static volatile uint32_t s_last_activity_s = 0;

// chime only on the first welcome
static volatile bool s_welcomed_once = false;

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
    // When true, line1/line2 are ignored and status_task instead renders a
    // looping robot_eyes animation (idle screen) until the next status_msg_t
    // arrives. Existing designated-initializer literals below don't set this,
    // so they default to false — no other call site needed to change.
    bool show_idle_eyes;
} status_msg_t;
static QueueHandle_t s_status_q;

// Real ceiling, not a nice round number: the panel's SPI bus is clocked at
// 4MHz single-line (st7789_init's pclk_hz) = ~500KB/s. Flushing only the
// eyes' dirty band (robot_eyes_dirty_band — about half the panel's rows;
// horizontally the two eyes already span nearly the full width at this
// geometry, so cropping columns too wouldn't save much more) is still
// ~57KB on a 240-wide panel, i.e. ~115ms/frame. This constant reflects
// that bus-bound ceiling (~8-9fps) rather than a rate this hardware can't
// actually deliver; only used while idle_eyes_active, so it doesn't affect
// the queue's normal portMAX_DELAY responsiveness to real status messages.
#define EYES_FRAME_MS 120
#define EYES_COLOR 0xFFFF
#define EYES_BG    0x0000

// How long the confirming text (e.g. "Ready"/"Press wake to talk") stays on
// screen before the eyes animation takes over. Without this, a show_idle_eyes
// message went straight to the animation and the text was never visible even
// for one frame — nothing confirmed WiFi/session state before the device
// looked "just idle".
#define IDLE_TEXT_HOLD_MS 1500

static void status_task(void *arg) {
    (void)arg;
    status_msg_t m;
    bool idle_eyes_active = false;
    // Allocated lazily from PSRAM on first use. Sized to the eyes' dirty
    // band (not the full panel — see robot_eyes_dirty_band) since that's
    // the only region ever redrawn. Boards without a pixel panel
    // (display_width()==0) never allocate it.
    static uint16_t *s_eyes_buf = NULL;

    for (;;) {
        TickType_t wait = idle_eyes_active ? pdMS_TO_TICKS(EYES_FRAME_MS) : portMAX_DELAY;
        if (xQueueReceive(s_status_q, &m, wait) == pdTRUE) {
            // Always show the text first — even for a show_idle_eyes message —
            // so the transition into idle is visible instead of instant.
            display_show(m.line1, m.has_line2 ? m.line2 : NULL);
            idle_eyes_active = m.show_idle_eyes && display_width() > 0 && display_height() > 0;
            if (idle_eyes_active) {
                vTaskDelay(pdMS_TO_TICKS(IDLE_TEXT_HOLD_MS));
            }
            if (m.play_voice) {
                // Mute the mic for the clip's duration + tail so the announcement
                // doesn't echo into STT (voice_play blocks until the clip is written).
                s_voice_busy = true;
                voice_play(m.voice);
                vTaskDelay(pdMS_TO_TICKS(SPK_TAIL_GUARD_MS));
                s_voice_busy = false;
            }
        }
        if (idle_eyes_active) {
            int w = display_width(), h = display_height();
            int band_y, band_h;
            robot_eyes_dirty_band(h, &band_y, &band_h);
            if (!s_eyes_buf) {
                s_eyes_buf = heap_caps_malloc((size_t)w * band_h * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
                if (!s_eyes_buf) {
                    // Text (m.line1/line2) is already showing from above —
                    // just stop trying to animate, don't overwrite it again.
                    ESP_LOGE(TAG, "robot_eyes: PSRAM alloc failed, falling back to text");
                    idle_eyes_active = false;
                    continue;
                }
            }
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            robot_eyes_render(s_eyes_buf, w, band_h, h, band_y, now_ms, EYES_COLOR, EYES_BG);
            display_flush(0, band_y, w, band_h, s_eyes_buf);
        }
    }
}

// Common "go idle" transition: same status text + show_idle_eyes everywhere,
// so no call site can forget to set the flag (a prior version of this file
// had 4 near-identical copies of this, and one — the volume-button handler
// — omitted it, silently killing the idle animation the moment volume was
// adjusted while idle).
static void send_idle_status(const char *line1) {
    status_msg_t m = { .play_voice = false, .has_line2 = true, .show_idle_eyes = true };
    strncpy(m.line1, line1, sizeof(m.line1) - 1);
    strncpy(m.line2, "Press wake to talk", sizeof(m.line2) - 1);
    xQueueSend(s_status_q, &m, 0);
}

// Runs in the button task context — only flips flags, adjusts the volume int,
// queues display messages, and sends a WS control frame (network, not
// hardware). It must never call display/audio hardware directly.
static void on_button(button_id_t id) {
    switch (id) {
    case BTN_WAKE: {
        if (s_state == APP_SPEAKING) {
            // Barge-in. Switch to LISTENING NOW so on_audio drops any further
            // downlink frames and the mic reopens; ask spk_task to do the actual
            // audio flush/reset (I2S + opus) — the button task must not touch audio
            // hardware from its small stack. Then tell the server to cancel the turn.
            // Connection stays open; do NOT toggle to Idle.
            s_turn_ending = false;
            s_state = APP_LISTENING;
            s_active = true;
            s_barge_in = true;   // spk_task flushes the queue + resets I2S/opus
            s_last_activity_s = (uint32_t)(esp_timer_get_time() / 1000000);
            ws_client_send_abort("user");
            status_msg_t m = { .play_voice = false, .has_line2 = true };
            strncpy(m.line1, "Listening", sizeof(m.line1) - 1);
            strncpy(m.line2, "Speak now", sizeof(m.line2) - 1);
            xQueueSend(s_status_q, &m, 0);
            break;
        }
        // Not speaking: toggle idle/listening. Waking re-enables the link (it may
        // be asleep after an idle goodbye); going idle puts it back to sleep.
        s_active = !s_active;
        ws_client_set_reconnect(s_active);
        s_last_activity_s = (uint32_t)(esp_timer_get_time() / 1000000);
        if (s_active) {
            status_msg_t m = { .play_voice = false, .has_line2 = true };
            strncpy(m.line1, "Listening", sizeof(m.line1) - 1);
            strncpy(m.line2, "Speak now", sizeof(m.line2) - 1);
            xQueueSend(s_status_q, &m, 0);
        } else {
            send_idle_status("Idle");
        }
        break;
    }
    case BTN_VOL_UP:
    case BTN_VOL_DOWN: {
        int v = audio_adjust_volume(id == BTN_VOL_UP ? 10 : -10);
        // show_idle_eyes = !s_active: if the device is idle, keep the eyes
        // animation running instead of showing this line (matches every
        // other idle-preserving path) — without this, adjusting volume
        // while idle used to permanently replace the animation with a
        // static "Volume NN%" line that nothing ever cleared.
        status_msg_t m = { .play_voice = false, .has_line2 = false, .show_idle_eyes = !s_active };
        snprintf(m.line1, sizeof(m.line1), "Volume %d%%", v);
        xQueueSend(s_status_q, &m, 0);
        break;
    }
    }
}

static void on_event(const lugo_event_t *ev) {
    s_last_activity_s = (uint32_t)(esp_timer_get_time() / 1000000);  // any server event = activity
    switch (ev->type) {
    case LUGO_EV_WELCOME: {
        s_state = APP_LISTENING;
        if (ev->idle_timeout_s > 0) s_idle_timeout_s = ev->idle_timeout_s;
        ESP_LOGI(TAG, "session ready (idle_timeout_s=%d)", ev->idle_timeout_s);
        // Chime only the first time; reconnect-welcomes (e.g. after an idle
        // goodbye) update the screen silently so an unattended device doesn't
        // chime on a loop. (Full sleep-until-wake is Phase 2 / MQTT.)
        status_msg_t m = { .play_voice = !s_welcomed_once, .voice = VOICE_CONNECTED, .has_line2 = true };
        s_welcomed_once = true;
        // If this welcome is a wake-triggered reconnect (user already active),
        // show Listening so the screen doesn't misleadingly say "Press wake".
        if (s_active) {
            strncpy(m.line1, "Listening", sizeof(m.line1) - 1);
            strncpy(m.line2, "Speak now", sizeof(m.line2) - 1);
        } else {
            strncpy(m.line1, "Connected", sizeof(m.line1) - 1);
            strncpy(m.line2, "Press wake to talk", sizeof(m.line2) - 1);
        }
        xQueueSend(s_status_q, &m, 0);
        break;
    }
    case LUGO_EV_STT:          ESP_LOGI(TAG, "you: %s", ev->text); break;
    case LUGO_EV_TTS_SENTENCE: ESP_LOGI(TAG, "bot: %s", ev->text); break;
    case LUGO_EV_TTS_START: {
        s_turn_ending = false;
        s_state = APP_SPEAKING;
        status_msg_t m = { .play_voice = false, .has_line2 = false };
        strncpy(m.line1, "Speaking", sizeof(m.line1) - 1);
        xQueueSend(s_status_q, &m, 0);
        break;
    }
    case LUGO_EV_TTS_STOP:
        // Turn ended (natural end OR server-side abort — both map to tts stop).
        // Don't open the mic yet: the jitter buffer may still be playing. Arm the
        // drain hand-off; spk_task returns us to LISTENING once empty. If nothing
        // is playing (text-only turn / already stopped by local barge-in), switch now.
        if (s_state == APP_SPEAKING) {
            s_turn_ending = true;  // spk_task drains, then flips to LISTENING + shows it
        } else {
            s_state = APP_LISTENING;
            if (s_active) {
                status_msg_t m = { .play_voice = false, .has_line2 = true };
                strncpy(m.line1, "Listening", sizeof(m.line1) - 1);
                strncpy(m.line2, "Speak now", sizeof(m.line2) - 1);
                xQueueSend(s_status_q, &m, 0);
            }
        }
        break;
    case LUGO_EV_GOODBYE: {
        // Server idle disconnect. Sleep: stop auto-reconnect so we don't
        // reconnect-storm every idle_timeout; the socket stays closed until the
        // user presses Wake. Go idle (mic muted).
        s_active = false;
        s_turn_ending = false;
        s_state = APP_LISTENING;
        ws_client_set_reconnect(false);
        { pkt_t *p; while (xQueueReceive(s_pktq, &p, 0) == pdTRUE) free(p); }  // flush
        audio_spk_reset();
        opus_codec_reset();
        send_idle_status("Idle");
        break;
    }
    case LUGO_EV_ERROR: {
        ESP_LOGE(TAG, "server error: %s", ev->text);
        status_msg_t m = { .play_voice = false, .has_line2 = true };
        strncpy(m.line1, "Error", sizeof(m.line1) - 1);
        // ev->text (256B) is wider than line2, so we intentionally truncate to
        // the display line. Bounded memcpy + explicit NUL keeps it safe and
        // avoids the riscv GCC -Werror=stringop/format-truncation on the C3 build.
        size_t elen = strnlen(ev->text, sizeof(m.line2) - 1);
        memcpy(m.line2, ev->text, elen);
        m.line2[elen] = '\0';
        xQueueSend(s_status_q, &m, 0);
        break;
    }
    case LUGO_EV_MCP: {
        if (ev->mcp_payload) {
            static char resp[MCP_FRAME_BUF_SIZE];
            int n = mcp_tools_dispatch(ev->mcp_payload, resp, sizeof resp);
            if (n > 0) ws_client_send_mcp(resp);
        }
        break;
    }
    default: break;  // LUGO_EV_UNKNOWN
    }
}

static void on_audio(const uint8_t *data, int len) {
    s_last_activity_s = (uint32_t)(esp_timer_get_time() / 1000000);
    if (len <= 0 || len > OPUS_MAX_PACKET) return;
    // Drop frames that arrive when we're not in a speaking turn: after a local
    // barge-in (state forced to LISTENING) this discards audio the server already
    // put on the wire before it received our abort, so the bot doesn't briefly
    // resume playing after the "stop."
    if (s_state != APP_SPEAKING) return;
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
        if (s_barge_in) {
            // Serviced here (not in the button task) because this task owns the
            // I2S TX channel + opus decoder. Flush the jitter queue, drop the
            // committed DMA, and reset the decoder so the next reply is clean.
            // The button task already set LISTENING + showed "Listening".
            s_barge_in = false;
            { pkt_t *bp; while (xQueueReceive(s_pktq, &bp, 0) == pdTRUE) free(bp); }
            audio_spk_reset();
            opus_codec_reset();
            priming = true;
        }
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
                if (s_active) {
                    status_msg_t m = { .play_voice = false, .has_line2 = true };
                    strncpy(m.line1, "Listening", sizeof(m.line1) - 1);
                    strncpy(m.line2, "Speak now", sizeof(m.line2) - 1);
                    xQueueSend(s_status_q, &m, 0);
                }
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
        uint32_t idle_s = (uint32_t)(esp_timer_get_time() / 1000000) - s_last_activity_s;
        if (idle_s >= (uint32_t)(to + 5)) {
            s_active = false;
            ws_client_set_reconnect(false);   // sleep the link too
            send_idle_status("Idle");
        }
    }
}

// One-time color-pipeline smoke test: 5 real color bars (not the pure
// white/black the eyes animation ever sends — a wrong RGB565 channel/bit
// order would be invisible with white/black alone, but not with these).
// Safe to call before status_task exists: app_main is still the only task
// touching the panel at this point.
static void show_boot_color_bars(void) {
    int w = display_width(), h = display_height();
    if (w <= 0 || h <= 0) return;   // no pixel panel on this board (flush unset)
    int band_h = h / 5;
    if (band_h < 1) band_h = 1;
    static uint16_t buf[320 * 64];  // one band at a time; covers panels up to 320 wide, 64 tall/band
    if ((size_t)w * (size_t)band_h > sizeof(buf) / sizeof(buf[0])) {
        ESP_LOGW(TAG, "boot color test: panel band too large for scratch buffer, skipping");
        return;
    }
    static const uint16_t colors[5] = {
        0xF800,  // red
        0x07E0,  // green
        0x001F,  // blue
        0xFFFF,  // white
        0xFFE0,  // yellow
    };
    for (int i = 0; i < 5; i++) {
        gfx_fill_rect(buf, w, band_h, 0, 0, w, band_h, colors[i]);
        display_flush(0, i * band_h, w, band_h, buf);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "esp32-assistant booting");
    ESP_ERROR_CHECK(board_detect_and_select());
    ESP_ERROR_CHECK(display_init());
    show_boot_color_bars();
    vTaskDelay(pdMS_TO_TICKS(2000));  // hold the bars long enough to actually see them
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

    display_show("WiFi OK", "Starting…");
    ESP_ERROR_CHECK(opus_codec_init());

    s_pktq = xQueueCreate(16, sizeof(pkt_t *));   // ~16*60ms buffer ceiling
    s_uplinkq = xQueueCreate(16, sizeof(uplink_pkt_t *));
    s_status_q = xQueueCreate(4, sizeof(status_msg_t));
    xTaskCreatePinnedToCore(status_task, "status", 8192, NULL, 4, NULL, APP_CPU_AUDIO);
    buttons_start(on_button);  // Wake toggles s_active; Vol +/- adjust volume
    xTaskCreate(idle_watchdog_task, "idle_wd", 3072, NULL, 3, NULL);

    // STT/TTS/language all come from the chatllm profile server-side; the device
    // configures only which profile to connect to (CONFIG_AA_PROFILE). Downlink
    // is decoded at 16 kHz to match the device opus decoder.
    s_last_activity_s = (uint32_t)(esp_timer_get_time() / 1000000);
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
    xTaskCreatePinnedToCore(spk_task, "spk", 16384, NULL, 6, NULL, APP_CPU_AUDIO);
    xTaskCreatePinnedToCore(mic_task, "mic", 40960, NULL, 5, NULL, APP_CPU_AUDIO);
    xTaskCreatePinnedToCore(uplink_task, "uplink", 16384, NULL, 5, NULL, APP_CPU_AUDIO);

    // Connect-on-wake: the WS stays closed (asleep) until the user presses Wake.
    // No gateway connection is held while idle. Routed through s_status_q (not
    // a direct display_show call) so it's status_task — not app_main — that
    // touches the panel, same isolation rule as every other idle transition;
    // status_task is already running by this point.
    send_idle_status("Ready");
    ESP_LOGI(TAG, "running (asleep — press wake to connect)");
}
