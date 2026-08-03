#include "wifi_sta.h"
#include "wifi_cfg.h"
#include "provisioning.h"
#include "display.h"
#include "robot_eyes.h"
#include "statusbar.h"
#include "battery.h"
#include "gfx.h"
#include "voice.h"
#include "buttons.h"
#include "board.h"
#include "nvs_flash.h"
#include "ws_client.h"
#include "audio.h"
#include "opus_codec.h"
#include "mcp_tools.h"
#include "pairing.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_system.h"
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
#define APP_CPU_UI    tskNO_AFFINITY
#else
#define APP_CPU_AUDIO 1
// status_task used to share core 1 with mic/spk. It never preempts them (prio
// 4 vs 5/6), but a display flush is not free to the tasks it runs beside: the
// render buffers live in PSRAM, so spi_master bounce-copies every chunk out of
// PSRAM into internal DMA memory, evicting D-cache lines and taking PSRAM bus
// cycles that opus_encode/opus_decode want. Keep core 1 for audio only and let
// the UI share core 0 with WiFi, where the work is mostly DMA wait anyway.
#define APP_CPU_UI    0
#endif

// Bool Kconfig options are undefined (not 0) when unset — provide a fallback.
#ifndef CONFIG_AA_SERVER_SECURE
#define CONFIG_AA_SERVER_SECURE 0
#endif

static const char *TAG = "app";

// Loaded once at boot (wifi_cfg_load) and never mutated again; file-scope so
// resolve_device_token() can reach server_host/server_port without threading
// app_main's local through every caller.
static wifi_cfg_t s_cfg;

// Resolved by resolve_device_token(): the NVS-stored per-device token, else
// whatever aa_run_pairing() just claimed (which also persists it to NVS
// itself).
static char s_device_token[128];

// GOODBYE reason text (e.g. "account_disabled"), captured by on_event() for
// aa_classify_disconnect() to inspect after the session ends. Reset both
// right after use (goodbye handling) and at the start of every new session
// (WELCOME) so a stale reason from an earlier session can never cause a
// false-positive repair classification on a later, unrelated disconnect.
static char s_last_goodbye_reason[64];

// Set by on_event()'s GOODBYE case when aa_classify_disconnect() says REPAIR.
// on_event() runs on the ws client's own task and must never block or touch
// the display directly (see the isolation note above status_task), so it
// only raises this flag; idle_watchdog_task (a normal, blockable task)
// services it.
static volatile bool s_repair_pending = false;

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

// "Tear the playback path down and start clean" request, serviced by spk_task
// — the owner of the I2S TX channel and the opus decoder — which flushes the
// jitter buffer, drops the committed DMA, and resets the decoder. One-shot flag.
//
// Two raisers, both of which must NOT do the work themselves:
//   - the button task (barge-in): its stack is small and cross-task I2S/SPI
//     calls have crashed here before.
//   - on_event's GOODBYE case: it runs on the ws client's task, so calling
//     audio_spk_reset() there blocks on the TX mutex behind an in-flight
//     i2s_channel_write, and opus_codec_reset() races spk_task's concurrent
//     opus_decode() on the same decoder — an unlocked read/write of the codec
//     state. It used to do exactly that inline; now it just raises this flag,
//     the same way barge-in always has.
static volatile bool s_audio_reset_req = false;

// Request that mic_task drop everything the RX DMA has already captured.
// Serviced by mic_task itself rather than by the requester, for the same reason
// s_barge_in is serviced by spk_task: the flush restarts the I2S RX channel, and
// mic_task is the task that owns (and normally sits blocked inside) that
// channel's read. Requesters only raise the flag.
//
// Why it's needed at all: mic_task reads a full 60ms frame BEFORE it re-checks
// s_state, so the frame in hand at a LISTENING transition was captured up to
// 60ms earlier — i.e. while the bot was still audible. That frame is what gets
// transcribed as self-talk. SPK_TAIL_GUARD_MS only hides this at the end of a
// normal turn (by making that 60ms fall inside the silent guard); on barge-in,
// where the state flips to LISTENING while the speaker DMA is still draining,
// nothing hid it at all.
// Set by: spk_task (turn drained; barge-in serviced) and on_button (barge-in,
// so the drop starts at the button press rather than whenever spk_task next
// runs). Cleared by mic_task.
static volatile bool s_mic_flush_req = false;

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

// One Opus packet, queued BY VALUE (xQueueSend copies it): the previous
// heap-pointer design malloc'd/free'd a packet on every 60ms frame in both
// directions, churning the internal heap right next to the WiFi stack's own
// allocations — a slow-fragmentation risk with zero upside. Both queues'
// storage lives in PSRAM (xQueueCreateStatic in app_main), which is why the
// slot size below matters: it is multiplied by the queue depth AND copied in
// full on every send and every receive.
// Shared by the downlink jitter buffer and the mic->uplink hand-off.
//
// AA_PKT_MAX is the per-slot payload capacity, and is deliberately NOT
// OPUS_MAX_PACKET (1500, the theoretical Opus ceiling). At 1500 each slot was
// 1.5KB, so every 60ms frame dragged 1.5KB through four copies — in PSRAM,
// evicting the D-cache lines the audio path wants — to move ~180B of real
// audio, and the two queues pinned 48KB+24KB of storage to match.
// What actually travels here is one Opus frame at 24kbps: ~180B at 60ms, or
// ~360B for the 120ms frames the gateway may send downlink (see
// OPUS_DOWN_SAMPLES_MAX). 640B keeps ~40% headroom over that worst case — a
// 120ms frame would have to exceed ~42kbps to overflow it — while cutting slot
// size, queue storage and per-frame copy cost by 2.3x.
// Anything larger is dropped with a log rather than silently truncated: see
// on_audio. If those warnings ever appear, the gateway's downlink bitrate
// changed and this number should follow it.
#define AA_PKT_MAX 640
typedef struct { int len; uint8_t data[AA_PKT_MAX]; } pkt_t;
// pkt_t is ~644B, so each queue's storage is PKT_QUEUE_DEPTH*644B. On the
// PSRAM-less C3 that storage falls back to internal RAM (see app_main), where
// every KB competes with mic_task's opus-encode stack for a large contiguous
// block — hence the much shallower depth there.
#if CONFIG_IDF_TARGET_ESP32C3
#define PKT_QUEUE_DEPTH 4     // C3: uplink only (downlink is the ring buffer); real-time paced, so shallow. Keeps internal RAM for the WS task.
#else
#define PKT_QUEUE_DEPTH 16
#endif
// Downlink jitter buffer depth (S3 pkt_t queue). The gateway paces Opus frames
// to real-time (conversation_opus_pace), so this only needs to absorb network
// jitter, not a whole burst reply — 32*644B ≈ 21KB in PSRAM. (If pacing is off
// the gateway floods and even 256 overflows on long replies; the fix is pacing,
// not a bigger buffer.) The C3 uses the compact ring buffer (DL_RB_BYTES).
#define DL_QUEUE_DEPTH 32
static QueueHandle_t s_uplinkq;   // mic->uplink hand-off (declared here; used below)

// Downlink diagnostics, written by the ws task in on_audio(), read+reset by
// spk_task at the end of each turn. Plain ints: single writer, and a torn read
// on a diagnostic counter would only misprint a log line.
// These exist because "words missing from the reply" has (at least) three
// distinct causes that look identical from the outside, and the code used to
// report NONE of them:
//   - the frame never fit our slot        -> s_dl_oversize (see AA_PKT_MAX)
//   - the frame arrived but the buffer was full -> s_dl_drops (this was the
//     root cause of the previous cut-audio bug, fixed by DL_QUEUE_DEPTH 16->32;
//     dl_push's return value has been discarded ever since, so a regression
//     here would be invisible)
//   - the buffer ran dry and playback stalled  -> spk_task's `underruns`
// s_dl_peak (high-water mark vs DL_QUEUE_DEPTH) says how much margin is left.
static volatile int s_dl_drops, s_dl_peak, s_dl_maxlen, s_dl_oversize;

// Downlink jitter buffer, behind dl_*() so the two targets differ in storage
// only. The gateway bursts a whole TTS reply (~74 frames) far faster than
// real-time playback, so the buffer must hold the burst, not just smooth jitter.
//   S3 (PSRAM, already working): the proven fixed-slot pkt_t queue, storage in
//     PSRAM, DL_QUEUE_DEPTH deep. Structurally unchanged; only the slot size
//     shrank (see AA_PKT_MAX).
//   C3 (no PSRAM): a fixed-slot queue deep enough for a burst can't fit internal
//     RAM even at 644B/slot, so use a NoSplit ring buffer that stores each Opus
//     frame at its true ~200B length — ~75 frames in 16KB, a full reply.
#if CONFIG_IDF_TARGET_ESP32C3
#include "freertos/ringbuf.h"
#define DL_RB_BYTES 16384   /* ~75 Opus frames (~4.5s) — a full TTS burst, within the C3 RAM budget */
static RingbufHandle_t s_dl_rb;
static volatile int s_dl_count;   // frames buffered (for prebuffer priming)
static inline void dl_init(void) { s_dl_rb = xRingbufferCreate(DL_RB_BYTES, RINGBUF_TYPE_NOSPLIT); configASSERT(s_dl_rb); }
static inline bool dl_push(const uint8_t *d, int len) {
    if (xRingbufferSend(s_dl_rb, d, (size_t)len, 0) != pdTRUE) return false;
    s_dl_count++; return true;
}
static inline bool dl_pop(uint8_t *out, int *len, TickType_t wait) {
    size_t sz = 0; void *it = xRingbufferReceive(s_dl_rb, &sz, wait);
    if (!it) return false;
    memcpy(out, it, sz); *len = (int)sz;
    vRingbufferReturnItem(s_dl_rb, it); s_dl_count--; return true;
}
static inline void dl_flush(void) { static uint8_t t[AA_PKT_MAX]; int l; while (dl_pop(t, &l, 0)) {} }
static inline int  dl_count(void) { return s_dl_count; }
#else
static QueueHandle_t s_pktq;   // jitter buffer; depth ~N*60ms ceiling
static inline void dl_init(void) { /* created in app_main (PSRAM static storage) */ }
static inline bool dl_push(const uint8_t *d, int len) {
    static pkt_t p; memcpy(p.data, d, (size_t)len); p.len = len;   // single writer (ws task)
    return xQueueSend(s_pktq, &p, 0) == pdTRUE;
}
static inline bool dl_pop(uint8_t *out, int *len, TickType_t wait) {
    static pkt_t p; if (xQueueReceive(s_pktq, &p, wait) != pdTRUE) return false;  // single reader (spk_task)
    memcpy(out, p.data, (size_t)p.len); *len = p.len; return true;
}
static inline void dl_flush(void) { xQueueReset(s_pktq); }
static inline int  dl_count(void) { return (int)uxQueueMessagesWaiting(s_pktq); }
#endif

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
    // Only meaningful when show_idle_eyes is true. Unset literals default to
    // ROBOT_EMOTION_NEUTRAL (enum value 0), so only call sites that want a
    // different expression need to set this explicitly.
    robot_emotion_t emotion;
} status_msg_t;
static QueueHandle_t s_status_q;

// Seconds since boot, the unit s_last_activity_s / s_idle_timeout_s work in
// (see s_last_activity_s for why seconds and not the raw microsecond counter).
static inline uint32_t now_s(void) {
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

// The one way to put something on screen. Every caller used to hand-roll the
// same four lines — a designated-initializer literal, one or two strncpy's, and
// an xQueueSend — thirteen times over, and the copies had already drifted: one
// forgot .show_idle_eyes (killing the idle animation whenever the volume was
// adjusted), and the two that copy server text had to grow their own bounded
// memcpy to keep the riscv build's -Werror=stringop-truncation quiet. Doing it
// in one place makes both problems structural rather than a review item.
//
// line2 == NULL means a one-line message (has_line2 stays false). Both lines
// are truncated to the fields' own widths; snprintf is used rather than strncpy
// precisely because it always terminates and never trips that C3 warning, so
// callers can pass a 256-byte `ev->text` straight in.
//
// `emotion` is only read when idle_eyes is true (see status_msg_t) — pass
// ROBOT_EMOTION_NEUTRAL for the plain text screens.
static void status_push(bool idle_eyes, robot_emotion_t emotion,
                        const char *line1, const char *line2) {
    status_msg_t m = {
        .play_voice = false,
        .has_line2 = line2 != NULL,
        .show_idle_eyes = idle_eyes,
        .emotion = emotion,
    };
    snprintf(m.line1, sizeof m.line1, "%s", line1);
    if (line2) snprintf(m.line2, sizeof m.line2, "%s", line2);
    xQueueSend(s_status_q, &m, 0);
}

// As status_push, plus a local voice clip played by status_task before it
// returns to the queue. Only the session-ready chime uses this today; kept
// separate so the common path doesn't carry an unused clip argument.
static void status_push_with_voice(voice_clip_t clip, robot_emotion_t emotion,
                                   const char *line1, const char *line2) {
    status_msg_t m = {
        .play_voice = true,
        .voice = clip,
        .has_line2 = line2 != NULL,
        .show_idle_eyes = true,
        .emotion = emotion,
    };
    snprintf(m.line1, sizeof m.line1, "%s", line1);
    if (line2) snprintf(m.line2, sizeof m.line2, "%s", line2);
    xQueueSend(s_status_q, &m, 0);
}

#if CONFIG_AA_MIC_METER
// TEMP mic meter (bring-up, local — do not commit): mic_task publishes its last
// read's sample count + peak amplitude; mic_meter_task shows them on the OLED so
// a silent vs working mic is visible without a serial console. Skips the gateway.
static volatile int s_mic_peak, s_mic_got;
static void mic_meter_task(void *arg) {
    (void)arg;
    for (;;) {
        char level[32];
        snprintf(level, sizeof level, "g %d p %d", s_mic_got, s_mic_peak);
        if (s_status_q) status_push(false, ROBOT_EMOTION_NEUTRAL, "Mic level", level);
        // Also to the console: the OLED alone means bring-up evidence has to be
        // read off the panel and relayed by hand. With a serial console attached
        // this is the same two numbers in a form that can be logged and diffed.
        ESP_LOGI(TAG, "mic meter: got=%d peak=%d", s_mic_got, s_mic_peak);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
#endif

// Restores whatever status line the volume overlay ("Volume NN%") is
// temporarily covering up, 0.5s after the last volume button press.
static esp_timer_handle_t s_volume_revert_timer;

// Animation tick while idle_eyes_active (doesn't affect the queue's normal
// portMAX_DELAY responsiveness to real status messages). Sized against the
// SPI budget: at 20MHz (st7789_init's pclk_hz, ~2.5MB/s) the eyes' dirty
// band (robot_eyes_dirty_band, ~57KB on a 240-wide panel) flushes in
// ~25ms, so 50ms/frame (~20fps) leaves comfortable headroom. Cheap in the
// common case regardless: each tick first computes robot_eyes_frame_key()
// and skips the render + flush entirely when nothing on screen would
// change (a static emotion between blinks — most of the device's idle
// life), so this tick rate only costs SPI bytes while genuinely animating.
#define EYES_FRAME_MS 50

// While idle the eyes tick is the only thing running, and nothing else
// repaints the status bar — without a periodic refresh the WiFi bars and
// battery gauge shown at the last status message stay frozen on screen for
// hours. RSSI/battery drift slowly; every 5s is plenty.
#define STATUS_BAR_REFRESH_MS 5000u

// Shared HUD palette: one continuous background across the status bar and
// eyes so there's no visible seam between the two regions. Status bar text
// stays white; the eyes render green on pure black (borderless — glow is
// set to 0 at boot in app_main).
#define HUD_FG 0xFFFF
#define HUD_BG 0x0000
#define EYES_COLOR 0x07E0   // pure green in RGB565
#define EYES_BG    HUD_BG

// Status bar strip height in pixels (WiFi bars + centered text + battery).
// A fixed 24px (already tuned against the 240x216 ST7789) eats over a third
// of a 128x64 OLED, squeezing the eyes/decor band underneath to almost
// nothing — scale down on short panels instead. 12 is the tightest that
// still fits every element without clipping: the 8px font glyph, the
// 8px-tall full wifi bar, and the 9px battery icon body all fit with >=1px
// margin at buf_h=12 (see statusbar_render's centering/margin math) — 6px
// (tried first) clipped all three, so the room for eyes/decor has to come
// from robot_eyes' own geometry instead, not from crushing the status bar
// further. display_height() is fixed for the device's whole lifetime, so
// this is safe to call repeatedly rather than cache.
static int status_bar_height(void) {
    return display_height() <= 64 ? 12 : 24;
}

// Rows the status bar actually occupies on the idle (eyes) screen — 0 on a
// 1-bit panel, where those 12 rows are 19% of the panel's height and the
// eyes need every row they can get (robot_eyes' geometry is pinned to
// panel_h/4 by the decor budget, so height handed to the eyes band is the
// ONLY way to grow them there). The cost is real: the bar carries the WiFi
// bars, the battery gauge AND the status text, so a mono panel shows none
// of those while idle — they come back with the next status message, which
// leaves the idle screen. Non-idle screens (display_show's text path) are
// untouched on every panel.
//
// Distinct from status_bar_height(), which stays the bar's own natural
// height and still sizes the scratch buffer even when nothing is drawn.
static int idle_status_bar_height(void) {
    return display_is_mono() ? 0 : status_bar_height();
}

// Renders + flushes the status bar strip (WiFi bars, centered text, battery).
// Factored out because two spots need it: every status message, and the
// periodic idle refresh (STATUS_BAR_REFRESH_MS).
// PSRAM first, internal RAM if the SoC has none. The C3 has no PSRAM at all,
// so a plain MALLOC_CAP_SPIRAM request there ALWAYS returns NULL — which meant
// every HUD buffer below failed to allocate, status_task fell back to the
// two-line text screen forever, and the idle eyes simply never rendered on any
// C3 board (with an ESP_LOGE on every status message to go with it). app_main's
// queue storage already falls back exactly like this for the same reason.
//
// Deliberately not "internal first": on the S3 these buffers are tens of KB and
// internal RAM is the scarce resource the audio path competes for, so PSRAM
// stays the preferred pool wherever it exists.
static void *hud_alloc(size_t bytes) {
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}

static void render_status_bar(uint16_t *bar_buf, int w, int bar_h, const char *line1) {
    int rssi = 0;
    bool connected = wifi_sta_get_rssi(&rssi);
    int bars = statusbar_wifi_bars(connected, rssi);
    // battery_read_pct()/battery_charge_state() are NULL-safe: a board with
    // no battery hardware wired (board_t.battery == NULL) gets
    // -1/BATTERY_NOT_CHARGING automatically, so this needs no per-board #ifdef.
    int batt_pct = battery_read_pct();
    bool charging = battery_charge_state() == BATTERY_CHARGING;
    statusbar_render(bar_buf, w, bar_h, bars, line1, batt_pct, charging, HUD_FG, HUD_BG);
    display_flush(0, 0, w, bar_h, bar_buf);
}

static void status_task(void *arg) {
    (void)arg;
    status_msg_t m;
    bool idle_eyes_active = false;
    // Frame-skip bookkeeping: the robot_eyes_frame_key / decor key of the
    // last frame actually flushed. force_frame covers everything a key can't
    // see (new status text, entering the HUD, decor buffer reallocation) —
    // any received message sets it. last_eyes_key 0 can never equal a real
    // key (bit 31 is always set there).
    uint32_t last_eyes_key = 0, last_decor_key = 0;
    bool force_frame = false;
    uint32_t last_bar_ms = 0;
    // Allocated lazily from PSRAM on first use. s_eyes_buf is sized to the
    // eyes' dirty band (not the full panel — see robot_eyes_dirty_band)
    // since that's the only region redrawn every frame; s_bar_buf covers
    // the status bar strip and doubles as scratch for the one-time
    // full-screen clear below. Boards without a pixel panel
    // (display_width()==0) never allocate either.
    static uint16_t *s_eyes_buf = NULL;
    static uint16_t *s_bar_buf = NULL;
    // Decor (mouth/"Zzz") buffer: reallocated only when a taller decor
    // shows up than whatever it's currently sized for (MOUTH and ZZZ use
    // different heights — see ROBOT_MOUTH_HEIGHT_PCT/ROBOT_ZZZ_HEIGHT_PCT
    // in robot_eyes.c), not on every emotion change.
    static uint16_t *s_decor_buf = NULL;
    static int s_decor_buf_h = 0;
    // MOUTH/ZZZ/WAVES each live in a different band (above vs. below the
    // eyes) — nothing else ever repaints those rows, so switching from one
    // decor to another (or to none) left the old one visibly stuck on
    // screen until this was tracked and explicitly cleared below.
    static robot_decor_t s_active_decor = ROBOT_DECOR_NONE;

    for (;;) {
        TickType_t wait = idle_eyes_active ? pdMS_TO_TICKS(EYES_FRAME_MS) : portMAX_DELAY;
        if (xQueueReceive(s_status_q, &m, wait) == pdTRUE) {
            bool was_idle_eyes = idle_eyes_active;
            idle_eyes_active = m.show_idle_eyes && display_width() > 0 && display_height() > 0;

            if (!idle_eyes_active) {
                display_show(m.line1, m.has_line2 ? m.line2 : NULL);
            } else {
                int w = display_width(), h = display_height();
                // Sized/filled at the bar's natural height even when the idle
                // screen draws no bar (mono): it doubles as the full-screen
                // clear scratch below, and a 0-row buffer would be a 0-byte
                // malloc feeding a flush loop that never advances.
                int scratch_h = status_bar_height();
                int status_bar_h = idle_status_bar_height();
                if (!s_bar_buf) {
                    s_bar_buf = hud_alloc((size_t)w * scratch_h * sizeof(uint16_t));
                }
                if (!s_bar_buf) {
                    ESP_LOGE(TAG, "statusbar: %ux%u alloc failed, falling back to text", (unsigned)w, (unsigned)scratch_h);
                    idle_eyes_active = false;
                    display_show(m.line1, m.has_line2 ? m.line2 : NULL);
                } else {
                    if (!was_idle_eyes) {
                        // One-time full-screen clear when entering the HUD
                        // layout (status bar + eyes) — otherwise leftover
                        // pixels from the old two-line text screen's
                        // different layout could show through in whatever
                        // neither region redraws (e.g. eyes-band margins).
                        // s_bar_buf is reused as an EYES_BG-filled scratch
                        // band; statusbar_render below overwrites it anyway.
                        gfx_fill_rect(s_bar_buf, w, scratch_h, 0, 0, w, scratch_h, EYES_BG);
                        for (int y = 0; y < h; y += scratch_h) {
                            int band = (y + scratch_h > h) ? h - y : scratch_h;
                            display_flush(0, y, w, band, s_bar_buf);
                        }
                    }
                    if (status_bar_h > 0) {
                        render_status_bar(s_bar_buf, w, status_bar_h, m.line1);
                    }
                    last_bar_ms = (uint32_t)(esp_timer_get_time() / 1000);
                    force_frame = true;   // new text/emotion: redraw eyes + decor too
                }
            }
            if (m.play_voice) {
                // Mute the mic for the clip's duration + tail so the announcement
                // doesn't echo into STT (voice_play blocks until the clip is written).
                s_voice_busy = true;
                voice_play(m.voice);
                vTaskDelay(pdMS_TO_TICKS(SPK_TAIL_GUARD_MS));
                // Same stale-frame edge as the end of a TTS turn (see
                // s_mic_flush_req): the frame mic_task is holding when the mute
                // lifts still contains the clip's tail. This is the exact path
                // that put "sẵn sàng" in front of what the user actually said.
                s_mic_flush_req = true;
                s_voice_busy = false;
            }
        }
        if (idle_eyes_active) {
            int w = display_width(), h = display_height();
            int status_bar_h = idle_status_bar_height();
            int eyes_h = h - status_bar_h;
            int band_y, band_h;
            robot_eyes_dirty_band(eyes_h, &band_y, &band_h);
            if (!s_eyes_buf) {
                s_eyes_buf = hud_alloc((size_t)w * band_h * sizeof(uint16_t));
                if (!s_eyes_buf) {
                    ESP_LOGE(TAG, "robot_eyes: %ux%u alloc failed, falling back to text", (unsigned)w, (unsigned)band_h);
                    idle_eyes_active = false;
                    display_show(m.line1, m.has_line2 ? m.line2 : NULL);
                    continue;
                }
            }
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            // Keep the otherwise-static status bar (WiFi bars / battery) alive
            // during long idles — nothing else repaints it between messages.
            if (status_bar_h > 0 && now_ms - last_bar_ms >= STATUS_BAR_REFRESH_MS) {
                last_bar_ms = now_ms;
                render_status_bar(s_bar_buf, w, status_bar_h, m.line1);
            }
            // Skip the render + SPI flush when this frame would be pixel-
            // identical to the one already on screen (equal frame key — see
            // robot_eyes.h). A static emotion between blinks skips >90% of
            // ticks; the idle screen is where the device spends most of its
            // life, so this is the difference between hammering the SPI bus
            // ~20x/s forever and touching it a few times per blink cycle.
            uint32_t eyes_key = robot_eyes_frame_key(m.emotion, now_ms);
            if (force_frame || eyes_key != last_eyes_key) {
                last_eyes_key = eyes_key;
                robot_eyes_render(s_eyes_buf, w, band_h, eyes_h, band_y, now_ms,
                                   m.emotion, EYES_COLOR, EYES_BG);
                display_flush(0, status_bar_h + band_y, w, band_h, s_eyes_buf);
            }

            // Decorations (mouth/"Zzz"/waves) are a separate,
            // independently-sized band deliberately NOT folded into the
            // eyes' own dirty band — most emotions have none, and always
            // paying for the extra region would tax every frame's
            // already-tight SPI budget for a decoration only some emotions
            // ever show.
            robot_decor_t decor = robot_eyes_decor_for(m.emotion);
            if (decor != s_active_decor && s_active_decor != ROBOT_DECOR_NONE) {
                // Switching away from a decor that lived in a DIFFERENT
                // band than the new one (MOUTH/WAVES are below the eyes,
                // ZZZ is above) — nothing else will ever repaint that old
                // band, so clear it explicitly or it lingers on screen.
                int old_y, old_h;
                robot_eyes_decor_band(eyes_h, s_active_decor, &old_y, &old_h);
                if (s_decor_buf && s_decor_buf_h >= old_h) {
                    gfx_fill_rect(s_decor_buf, w, old_h, 0, 0, w, old_h, EYES_BG);
                    display_flush(0, status_bar_h + old_y, w, old_h, s_decor_buf);
                }
            }
            s_active_decor = decor;
            if (decor != ROBOT_DECOR_NONE) {
                int decor_y, decor_h;
                robot_eyes_decor_band(eyes_h, decor, &decor_y, &decor_h);
                if (!s_decor_buf || s_decor_buf_h < decor_h) {
                    if (s_decor_buf) heap_caps_free(s_decor_buf);
                    s_decor_buf = hud_alloc((size_t)w * decor_h * sizeof(uint16_t));
                    s_decor_buf_h = s_decor_buf ? decor_h : 0;
                }
                if (s_decor_buf) {
                    // Same skip-if-unchanged gate as the eyes above (MOUTH
                    // only really changes on its 300ms flap edges).
                    uint32_t decor_key = robot_eyes_decor_frame_key(decor, now_ms);
                    if (force_frame || decor_key != last_decor_key) {
                        last_decor_key = decor_key;
                        robot_eyes_render_decor(s_decor_buf, w, decor_h, eyes_h, decor_y,
                                                 now_ms, decor, EYES_COLOR, EYES_BG);
                        display_flush(0, status_bar_h + decor_y, w, decor_h, s_decor_buf);
                    }
                }
            }
            force_frame = false;
        }
    }
}

// Common "go idle" transition: same status text + show_idle_eyes everywhere,
// so no call site can forget to set the flag (a prior version of this file
// had 4 near-identical copies of this, and one — the volume-button handler
// — omitted it, silently killing the idle animation the moment volume was
// adjusted while idle).
static void send_idle_status(const char *line1) {
    status_push(true, ROBOT_EMOTION_SLEEPY, line1, "Press wake to talk");
}

// Common "now listening" HUD update (SURPRISED eyes + "Speak now"), shared by
// every transition that reopens the mic: wake, barge-in, a text-only TTS_STOP,
// the post-drain hand-off in spk_task, and the volume-overlay revert. Same
// dedup rationale as send_idle_status above — a hand-rolled copy of this
// block existed at all five sites and only needed to drift once to bug out.
static void send_listening_status(void) {
    status_push(true, ROBOT_EMOTION_SURPRISED, "Listening", "Speak now");
}

// mcp_tools' self.screen.show_text callback (registered via
// mcp_tools_set_show_text_hook below). Same reason show_pair_code below is
// queued rather than drawn directly, but load-bearing here: the tool function
// runs inside on_event(), on the ws client's task, and status_task is already
// driving the panel ~20x/s for the idle eyes -- two tasks issuing esp_lcd
// transactions against one panel handle is not safe. Queueing hands the text
// to the one task allowed to draw.
//
// show_idle_eyes stays false so this renders as the plain two-line text
// screen, i.e. exactly what the tool's description promises; the next status
// message (TTS_START, a button, ...) replaces it as usual.
static void show_mcp_text(const char *line1, const char *line2) {
    status_push(false, ROBOT_EMOTION_NEUTRAL, line1, line2);
}

// aa_run_pairing's show callback. Routed through s_status_q rather than a
// direct display_show() -- resolve_device_token() can run well after
// status_task has started (both at boot, right before ws_client_start, and
// later from idle_watchdog_task after a revoke), so it must obey the same
// "only status_task touches the panel" rule as everything else here.
static void show_pair_code(const char *code) {
    status_push(false, ROBOT_EMOTION_NEUTRAL, "Pair code", code);
}

// Mute the mic, stop auto-reconnect (the socket stays closed until the next
// wake) and re-enable modem power-save. The shared prefix of EVERY path that
// ends a conversation — the Wake button toggling off, the MCP idle tool, the
// server's goodbye, the device-side idle watchdog, and do_repair. Each of
// those hand-rolled these three lines, and they had already drifted: two of
// them forgot to refresh s_last_activity_s.
//
// Deliberately does not touch the screen: callers differ there (idle screen,
// "Unpaired", or nothing at all while a repair is pending).
static void sleep_link(void) {
    s_active = false;
    ws_client_set_reconnect(false);
    wifi_sta_set_perf_mode(false);
    s_last_activity_s = now_s();
}

// Resolves the token ws_client_start() connects with:
//   1) a per-device token already claimed and stored in NVS.
//   2) aa_run_pairing(): blocks (HTTP poll loop) until an operator claims the
//      code shown via show_pair_code(); persists the token to NVS itself.
//
// There is no build-time override any more. CONFIG_AA_DEVICE_TOKEN used to sit
// in front of both of these, and its whole existence was a special case:
// because it lived in the image rather than in NVS there was nothing to clear,
// so a revoked override could never be re-paired away and BOTH revoke paths
// (on_event's GOODBYE case and idle_watchdog_task) needed a guard to skip
// themselves whenever it was set. Pairing is the mechanism now, so the option
// and its two guards are gone.
static const char *resolve_device_token(void) {
    if (aa_load_device_token(s_device_token, sizeof s_device_token) > 0)
        return s_device_token;

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char serial[13];
    aa_format_serial(mac, serial);
    // Sized for the worst case GCC's -Werror=format-truncation can statically
    // see: s_cfg.server_host is a fixed char[WIFI_CFG_HOST_MAX+1] (128) array
    // (see wifi_cfg_types.h), not just a pointer -- "https" + "://" +
    // 127-char host + ":" + a port's digits + NUL comfortably fits in 200.
    char base[200];
    snprintf(base, sizeof base, "%s://%s:%d",
             CONFIG_AA_SERVER_SECURE ? "https" : "http",
             s_cfg.server_host, s_cfg.server_port);
    aa_run_pairing(base, serial, show_pair_code, s_device_token, sizeof s_device_token);
    return s_device_token;
}

// Services a detected revoke (see s_repair_pending / idle_watchdog_task):
// stop the link, show it on screen, wipe the NVS token, block in pairing for
// a fresh one, then reboot. Reboot rather than an in-place ws_client restart
// because ws_client exposes no safe "swap the token and reconnect" primitive
// -- calling ws_client_start() a second time would re-init a new
// esp_websocket_client handle (leaking the old one) and spawn a second
// ws_conn_task racing the first over the same shared statics in ws_client.c.
// A reboot is clean, and the new token is already in NVS by the time
// resolve_device_token() (called again next boot) looks for it.
static void do_repair(const char *why) {
    ESP_LOGW(TAG, "device token invalid (%s) -- clearing and re-pairing", why);
    sleep_link();
    status_push(false, ROBOT_EMOTION_NEUTRAL, "Unpaired", "re-pairing");
    aa_clear_device_token();
    resolve_device_token();   // blocks until claimed; show_pair_code shows the fresh code
    esp_restart();
}

// Common "go idle" transition: stop auto-reconnect, mute the mic, show the
// idle screen. Shared by the Wake button (toggling off) and the MCP
// self.device.idle tool (registered via mcp_tools_set_idle_hook below) so a
// voice "go to sleep" request drives the same real transition as the button.
static void go_idle(void) {
    sleep_link();
    send_idle_status("Idle");
}

// The mirror image of go_idle: re-enable the link (it may be asleep after an
// idle goodbye), unmute the mic, and drop modem power-save for a steadier
// audio RTT while conversing. Shared by the Wake button and the boot-time
// CONFIG_AA_AUTO_WAKE path, which carried a verbatim copy of these five lines.
static void go_active(void) {
    s_active = true;
    ws_client_set_reconnect(true);
    wifi_sta_set_perf_mode(true);
    s_last_activity_s = now_s();
    send_listening_status();
}

// Voice-driven "let's start over": ask the gateway to end this conversation and
// open a fresh one on the SAME socket (docs/api.md, `new_session`). Reconnecting
// instead would work too, but costs a full handshake plus the engine-warm wait,
// and — because the gateway re-resolves the profile on connect — would drop the
// reply the user is mid-way through hearing.
//
// Called from the LUGO_EV_MCP handler once the self.session.new tool result has
// already been written to the socket (mcp_tools_take_new_session_request), never
// from inside the tool function: the request has to arrive BEHIND the result the
// model is waiting on, or the gateway sees the rotation first and the confirming
// reply never happens.
static void start_new_conversation(void) {
    ws_client_send_new_session();
}

// Common "Volume NN%" overlay + auto-revert arm. Shared by the Vol +/-
// buttons and the MCP self.audio.set_volume tool (registered via
// mcp_tools_set_volume_hook below) so a voice-driven volume change gets the
// same on-screen feedback as a physical button press.
static void show_volume_overlay(int v) {
    char line[32];
    snprintf(line, sizeof line, "Volume %d%%", v);
    status_push(true, ROBOT_EMOTION_NEUTRAL, line, NULL);
    // (Re)arm the revert timer so repeated changes keep pushing it out — the
    // overlay only reverts 0.5s after the LAST change, not the first.
    esp_timer_stop(s_volume_revert_timer);  // no-op (ESP_ERR_INVALID_STATE) if not running
    esp_timer_start_once(s_volume_revert_timer, 500000);
}

// esp_timer callback (runs on the esp_timer service task, not the button task) —
// fires 0.5s after the last volume press and restores the status line that was
// showing before "Volume NN%" covered it. Re-derives it from s_state/s_active
// instead of caching the prior text, so it can't go stale if state changed
// while the overlay was up (e.g. TTS started mid-adjustment).
static void volume_revert_cb(void *arg) {
    (void)arg;
    if (s_state == APP_SPEAKING) {
        status_push(true, ROBOT_EMOTION_HAPPY, "Speaking", NULL);
    } else if (s_active) {
        send_listening_status();
    } else {
        send_idle_status("Idle");
    }
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
            s_audio_reset_req = true;   // spk_task flushes the queue + resets I2S/opus
            // Start dropping captured audio from the press itself: the speaker
            // is still playing the bot until spk_task services s_barge_in, and
            // the mic is already open (state is LISTENING as of the line above).
            s_mic_flush_req = true;
            s_last_activity_s = now_s();
            ws_client_send_abort("user");
            send_listening_status();
            break;
        }
        // Not speaking: toggle idle/listening. Waking re-enables the link (it may
        // be asleep after an idle goodbye); going idle puts it back to sleep.
        if (s_active) go_idle(); else go_active();
        break;
    }
    case BTN_VOL_UP:
    case BTN_VOL_DOWN: {
        // The HUD (status bar + eyes) is now the universal display mode for
        // every conversational state, so volume feedback shows through the
        // status bar text like everything else instead of a special-cased
        // full-text screen.
        show_volume_overlay(audio_adjust_volume(id == BTN_VOL_UP ? 10 : -10));
        break;
    }
    case BTN_EMOTION: {
        // Demo/tuning aid: each click shows a different random emotion (the
        // random pick lives HERE — robot_eyes itself stays RNG-free and
        // deterministic). Re-rolling the same value as last time would look
        // like a dead button, so bump to the next state instead.
        static robot_emotion_t s_last = ROBOT_EMOTION_COUNT;   // "none yet"
        robot_emotion_t e = (robot_emotion_t)(esp_random() % ROBOT_EMOTION_COUNT);
        if (e == s_last) e = (robot_emotion_t)((e + 1) % ROBOT_EMOTION_COUNT);
        s_last = e;
        status_push(true, e, robot_eyes_emotion_name(e), NULL);
        break;
    }
    }
}

static void on_event(const lugo_event_t *ev) {
    s_last_activity_s = now_s();   // any server event = activity
    switch (ev->type) {
    case LUGO_EV_WELCOME: {
        // New session starting: any goodbye reason left over from a prior
        // session (e.g. one that dropped without ever reaching the GOODBYE
        // case below) must not leak into this session's disconnect
        // classification later.
        s_last_goodbye_reason[0] = '\0';
        s_state = APP_LISTENING;
        if (ev->idle_timeout_s > 0) s_idle_timeout_s = ev->idle_timeout_s;
        ESP_LOGI(TAG, "session ready (idle_timeout_s=%d)", ev->idle_timeout_s);
        // Chime only the first time; reconnect-welcomes (e.g. after an idle
        // goodbye) update the screen silently so an unattended device doesn't
        // chime on a loop. (Full sleep-until-wake is Phase 2 / MQTT.)
        bool chime = !s_welcomed_once;
        s_welcomed_once = true;
        // If this welcome is a wake-triggered reconnect (user already active),
        // show Listening so the screen doesn't misleadingly say "Press wake".
        robot_emotion_t emotion = s_active ? ROBOT_EMOTION_SURPRISED : ROBOT_EMOTION_NEUTRAL;
        const char *l1 = s_active ? "Listening" : "Connected";
        const char *l2 = s_active ? "Speak now" : "Press wake to talk";
        if (chime) status_push_with_voice(VOICE_CONNECTED, emotion, l1, l2);
        else       status_push(true, emotion, l1, l2);
        break;
    }
    case LUGO_EV_STT: {
        ESP_LOGI(TAG, "you: %s", ev->text);
        // The server has the transcript and is now running the LLM + TTS —
        // seconds of work (measured: ~10s on a long reply) during which nothing
        // else updated the screen, so it kept showing "Listening / Speak now"
        // while the device was really just waiting. Show that it's working, and
        // show what it heard.
        //
        // Display-only, deliberately: s_state stays APP_LISTENING so mic_task's
        // gate (s_state != APP_LISTENING) is untouched and the uplink keeps
        // running exactly as before — adding a real APP_THINKING state would
        // have silently muted the mic here.
        // Nothing needs to clear this screen: every path out of it (TTS_START,
        // TTS_STOP, GOODBYE, ERROR, the Wake button) already pushes its own
        // status message, so there is no way to get stuck on it and no timeout
        // to maintain.
        // ev->text (256B) is wider than status_msg_t.line2; status_push
        // truncates it safely (snprintf), which is also what keeps GCC's
        // -Werror=stringop-truncation quiet on the riscv (C3) build.
        status_push(true, ROBOT_EMOTION_PONDERING, "Thinking", ev->text);
        break;
    }
    case LUGO_EV_TTS_SENTENCE: ESP_LOGI(TAG, "bot: %s", ev->text); break;
    case LUGO_EV_TTS_START: {
        s_turn_ending = false;
        s_state = APP_SPEAKING;
        status_push(true, ROBOT_EMOTION_HAPPY, "Speaking", NULL);
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
            if (s_active) send_listening_status();
        }
        break;
    case LUGO_EV_GOODBYE: {
        // Capture the reason now (ev->text) for the classify check below --
        // ev is only valid for the duration of this callback.
        strncpy(s_last_goodbye_reason, ev->text, sizeof(s_last_goodbye_reason) - 1);
        s_last_goodbye_reason[sizeof(s_last_goodbye_reason) - 1] = '\0';
        // Server idle disconnect. Sleep: stop auto-reconnect so we don't
        // reconnect-storm every idle_timeout; the socket stays closed until the
        // user presses Wake. Go idle (mic muted) — but not go_idle(), because
        // the idle screen must wait until we know this isn't a revoke.
        sleep_link();
        s_turn_ending = false;
        s_state = APP_LISTENING;
        // Ask spk_task to flush the downlink buffer + reset I2S/opus, rather
        // than doing it here: this callback runs on the ws client's task and
        // spk_task may be inside opus_decode()/i2s_channel_write() right now
        // (see s_audio_reset_req). Doing it inline raced the decoder state and
        // blocked this callback on the speaker's TX mutex.
        s_audio_reset_req = true;
        // Revoke check (reason=account_disabled -> wipe the NVS token and
        // re-pair). Every token is a per-device NVS one now, so this no longer
        // needs the "unless it's a build-time override" escape hatch.
        if (aa_classify_disconnect(ws_client_last_handshake_status(),
                                    s_last_goodbye_reason) == AA_DISCONNECT_REPAIR) {
            // Defer the actual clear/re-pair/reboot to idle_watchdog_task:
            // this callback runs on the ws client's own task and must not
            // block (HTTP + waiting on an operator to claim the code).
            s_repair_pending = true;
        } else {
            send_idle_status("Idle");
        }
        s_last_goodbye_reason[0] = '\0';   // consumed; reset for the next session
        break;
    }
    case LUGO_EV_ERROR: {
        ESP_LOGE(TAG, "server error: %s", ev->text);
        // ev->text (256B) is wider than line2: status_push truncates it to the
        // display line (see the STT case above for the -Werror note).
        status_push(false, ROBOT_EMOTION_NEUTRAL, "Error", ev->text);
        break;
    }
    case LUGO_EV_MCP: {
        if (ev->mcp_payload) {
            static char resp[MCP_FRAME_BUF_SIZE];
            int n = mcp_tools_dispatch(ev->mcp_payload, resp, sizeof resp);
            if (n > 0) ws_client_send_mcp(resp);
            // AFTER the result, never before: see start_new_conversation.
            if (mcp_tools_take_new_session_request()) start_new_conversation();
        }
        break;
    }
    default: break;  // LUGO_EV_UNKNOWN
    }
}

static void on_audio(const uint8_t *data, int len) {
    s_last_activity_s = now_s();
    if (len <= 0) return;
    if (len > AA_PKT_MAX) {
        // Louder than a silent drop: a frame this big means the gateway's
        // downlink bitrate/frame size outgrew AA_PKT_MAX, and the symptom
        // downstream would be mystery gaps in playback. Rate-limited to the
        // first occurrence so a sustained mismatch can't flood the console
        // from the ws task.
        s_dl_oversize++;
        static bool warned;
        if (!warned) { warned = true; ESP_LOGW(TAG, "downlink frame %d B > AA_PKT_MAX %d — dropping", len, AA_PKT_MAX); }
        return;
    }
    if (len > s_dl_maxlen) s_dl_maxlen = len;   // what the gateway actually sends
    // Drop frames that arrive when we're not in a speaking turn: after a local
    // barge-in (state forced to LISTENING) this discards audio the server already
    // put on the wire before it received our abort, so the bot doesn't briefly
    // resume playing after the "stop."
    if (s_state != APP_SPEAKING) return;
    // dl_push copies the frame into the downlink buffer (single writer: the ws
    // task). A full buffer drops the frame — which is audible as a missing word,
    // so count it instead of discarding the return value silently.
    if (!dl_push(data, len)) s_dl_drops++;
    int q = dl_count();
    if (q > s_dl_peak) s_dl_peak = q;
}

// Mirrors xiaozhi-esp32's architecture: the audio-capture task only encodes
// and queues; a separate uplink_task owns the ws_client_send_audio() call.
// (opus_encode is the stack-heavy part — see mic_task's stack size below.)
// s_uplinkq is declared up top (near the downlink buffer).

static void mic_task(void *arg) {
    (void)arg;
    int16_t pcm[OPUS_UP_SAMPLES];
    static pkt_t p;   // single mic_task instance; xQueueSend copies it out
    for (;;) {
        int got = audio_mic_read(pcm, OPUS_UP_SAMPLES);   // keeps I2S draining always
        // Checked AFTER the read, so the frame in hand — captured before the
        // requester flipped us back to LISTENING — is discarded along with the
        // DMA's contents, instead of being encoded and sent as user speech.
        if (s_mic_flush_req) {
            s_mic_flush_req = false;
            audio_mic_flush();
            continue;
        }
#if CONFIG_AA_MIC_METER
        { int peak = 0;
          for (int i = 0; i < got && i < OPUS_UP_SAMPLES; i++) { int a = pcm[i] < 0 ? -pcm[i] : pcm[i]; if (a > peak) peak = a; }
          s_mic_peak = peak; s_mic_got = got; }
#endif
        if (got != OPUS_UP_SAMPLES) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        // Only stream when the user has activated the conversation (Wake button),
        // the session is ready, and we're not playing the bot (half-duplex).
        if (!s_active || s_state != APP_LISTENING || s_voice_busy || !ws_client_connected()) continue;
        int n = opus_codec_encode(pcm, p.data, sizeof p.data);
        if (n > 0) {
            p.len = n;
            if (xQueueSend(s_uplinkq, &p, 0) != pdTRUE) {
                // A full queue means uplink_task is stuck inside
                // ws_client_send_audio (a blocked TCP write — see the
                // portMAX_DELAY note there), so it is PKT_QUEUE_DEPTH frames
                // behind: ~1s on the S3, ~240ms on the C3.
                // The old behaviour here was a plain drop-on-overflow, which
                // discards the NEWEST frame and keeps the oldest — backwards
                // for real-time audio. It also never recovers: once full, the
                // queue stays full, so every frame that does get through is
                // delivered a fixed second late for the rest of the session,
                // and the conversation stays permanently out of sync.
                // Drop the whole backlog instead and restart from the current
                // frame: the audio in there is stale by definition, and
                // resyncing costs one gap rather than a permanent offset.
                UBaseType_t dropped = uxQueueMessagesWaiting(s_uplinkq);
                xQueueReset(s_uplinkq);
                xQueueSend(s_uplinkq, &p, 0);
                ESP_LOGW(TAG, "uplink stalled — dropped %u stale frame(s) to resync",
                         (unsigned)dropped);
            }
        }
    }
}

static void uplink_task(void *arg) {
    (void)arg;
    static pkt_t p;   // single uplink_task instance
    for (;;) {
        if (xQueueReceive(s_uplinkq, &p, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        ws_client_send_audio(p.data, p.len);
    }
}

static void spk_task(void *arg) {
    (void)arg;
    int16_t pcm[OPUS_DOWN_SAMPLES_MAX];  // must fit the largest (120ms) Opus frame
                                         // the gateway may send, not just 60ms —
                                         // undersizing this stack-smashed spk_task.
    static pkt_t p;   // single spk_task instance; xQueueReceive copies into it
    bool priming = true;   // build slack before the first frame of each burst
    // Mid-reply underruns in the current turn: the buffer ran dry while the bot
    // was still speaking, so playback stalled and re-primed (an audible gap).
    // Logged per turn because it is the number that decides whether
    // SPK_PREBUFFER_FRAMES can come down: a steady 0 across turns means the 4
    // frames (240ms) of startup latency are buying nothing and can be traded
    // for responsiveness; anything above 0 means the depth is already marginal.
    int underruns = 0;
    for (;;) {
        if (s_audio_reset_req) {
            // Serviced here (not in the requester) because this task owns the
            // I2S TX channel + opus decoder. Flush the jitter queue, drop the
            // committed DMA, and reset the decoder so the next reply is clean.
            // Raised by barge-in (the button task already set LISTENING +
            // showed "Listening") and by the goodbye handler.
            s_audio_reset_req = false;
            dl_flush();
            audio_spk_reset();
            opus_codec_reset();
            // The speaker only actually goes quiet on the audio_spk_reset()
            // above; everything the mic captured up to here is the bot's voice
            // (on_button already reopened the mic at the button press). Ask for
            // a second flush at this edge — the one on_button raised covered the
            // press itself, this one covers the DMA drain since.
            s_mic_flush_req = true;
            priming = true;
            underruns = 0;
        }
        if (priming) {
            // Wait for buffer slack before draining. Exceptions that start playback
            // early: enough frames buffered (q >= prime depth), or the turn is ending
            // (s_turn_ending) so the buffered tail must flush even if short. q==0 just
            // keeps us idle here. Keyed on s_turn_ending, not s_state, because the turn
            // now stays APP_SPEAKING until this task drains it.
            int q = dl_count();
            if (q == 0 || (q < SPK_PREBUFFER_FRAMES && !s_turn_ending)) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            priming = false;
        }
        if (!dl_pop(p.data, &p.len, pdMS_TO_TICKS(100))) {
            // Ran dry mid-reply (still SPEAKING, turn not ending): that is the
            // audible stutter SPK_PREBUFFER_FRAMES exists to prevent. Counted
            // here, reported at the end of the turn below.
            if (s_state == APP_SPEAKING && !s_turn_ending) underruns++;
            priming = true;   // buffer drained — re-prime before the next burst
            if (s_turn_ending) {
                // Push the last real frames fully out with a short silence tail so
                // auto_clear doesn't silence the final syllable at the underrun
                // boundary (audio_spk_write blocks, so the silence follows the last
                // real audio through the DMA — tail plays in full, then quiet).
                static const int16_t silence[240] = {0};
                for (int i = 0; i < 8; i++) audio_spk_write(silence, 240);  // ~120ms
                // Let the last I2S DMA buffer finish and the room echo decay before
                // reopening the mic, so we don't transcribe our own trailing audio.
                vTaskDelay(pdMS_TO_TICKS(SPK_TAIL_GUARD_MS));
                // Everything captured during the guard is our own speech decaying
                // in the room; drop it so the first uplink frame starts clean.
                // Raised before the state flip so mic_task can service it as soon
                // as the mic reopens.
                s_mic_flush_req = true;
                s_turn_ending = false;
                s_state = APP_LISTENING;
                // One line that separates the three ways a reply loses audio —
                // see the s_dl_* declarations.
                ESP_LOGI(TAG, "turn done: underruns=%d dl_drops=%d oversize=%d "
                              "peakq=%d/%d maxlen=%dB prebuffer=%d",
                         underruns, s_dl_drops, s_dl_oversize,
                         s_dl_peak, DL_QUEUE_DEPTH, s_dl_maxlen, SPK_PREBUFFER_FRAMES);
                underruns = 0;
                s_dl_drops = s_dl_peak = s_dl_oversize = 0;
                // s_dl_maxlen deliberately NOT reset: it is a property of the
                // gateway's encoder, not of this turn, and the useful question
                // is the largest frame ever seen against AA_PKT_MAX.
                if (s_active) send_listening_status();
            }
            continue;
        }
        int n = opus_codec_decode(p.data, p.len, pcm);
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

        // Serviced here rather than in on_event (see s_repair_pending's
        // comment): a goodbye-carried revoke (reason=account_disabled).
        if (s_repair_pending) {
            s_repair_pending = false;
            do_repair("goodbye: account_disabled");   // does not return (esp_restart)
        }

        // Handshake-rejected-before-CONNECTED revoke (e.g. 401/403): no
        // LUGO_EV_GOODBYE is ever delivered for this case -- the session
        // never reached the Lugo protocol layer at all, so this periodic
        // check is the only place that can notice it. Only while the user is
        // actively trying to connect (s_active).
        if (s_active && !ws_client_connected()) {
            int status = ws_client_last_handshake_status();
            if (aa_classify_disconnect(status, "") == AA_DISCONNECT_REPAIR) {
                do_repair("handshake rejected");   // does not return (esp_restart)
            }
        }

        int to = s_idle_timeout_s;
        if (!s_active || to <= 0) continue;
        uint32_t idle_s = now_s() - s_last_activity_s;
        if (idle_s >= (uint32_t)(to + 5)) go_idle();
    }
}

// Bool Kconfig options are undefined (not 0) when unset — same fallback
// pattern as CONFIG_AA_SERVER_SECURE above.
#ifndef CONFIG_AA_BOOT_COLOR_BARS
#define CONFIG_AA_BOOT_COLOR_BARS 0
#endif

#if CONFIG_AA_BOOT_COLOR_BARS
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
    // PSRAM, not a static internal-RAM array: this only runs once at boot,
    // right before audio_init() sets up the mic's I2S DMA — a large `static`
    // buffer here permanently reserves internal SRAM for the rest of the
    // device's life (static storage is allocated whether or not this
    // function ever runs), competing with whatever internal RAM the I2S
    // driver wants. Same MALLOC_CAP_SPIRAM already proven safe for the same
    // display_flush path by the robot_eyes buffer.
    uint16_t *buf = heap_caps_malloc((size_t)w * band_h * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGW(TAG, "boot color test: PSRAM alloc failed, skipping");
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
    heap_caps_free(buf);
}
#endif  // CONFIG_AA_BOOT_COLOR_BARS

void app_main(void) {
    ESP_LOGI(TAG, "esp32-assistant booting");
    robot_eyes_set_glow_pct(0);   // borderless eyes — no glow halo
    ESP_ERROR_CHECK(board_select_configured());
    // Non-fatal: a missing/miswired panel must not boot-loop the device. The
    // driver runs headless (show/flush become no-ops) so audio/WiFi still come
    // up. See ssd1306_init's i2c scan log to diagnose the panel.
    esp_err_t disp_err = display_init();
    if (disp_err != ESP_OK) ESP_LOGW(TAG, "display_init failed (%s) — continuing headless", esp_err_to_name(disp_err));
#if CONFIG_AA_BOOT_COLOR_BARS
    show_boot_color_bars();
    vTaskDelay(pdMS_TO_TICKS(2000));  // hold the bars long enough to actually see them
#endif
    ESP_ERROR_CHECK(audio_init());  // moved earlier: voice_play() needs the codec
                                     // ready before the first status announcement,
                                     // and audio_init() has no WiFi dependency.
    ESP_ERROR_CHECK(battery_init());  // no-op if this board has no battery hardware

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // s_cfg is file-scope (not a local `cfg`) so resolve_device_token() can
    // read server_host/server_port later without needing app_main to pass it
    // down explicitly. Loaded once here and never mutated again.
    ESP_ERROR_CHECK(wifi_cfg_load(&s_cfg));

    // wifi_sta_start runs even with an empty SSID: provisioning_start needs a
    // started, STA-mode radio to scan for networks before it flips to AP mode.
    ESP_ERROR_CHECK(wifi_sta_start(s_cfg.ssid, s_cfg.password));
    if (s_cfg.ssid[0] == '\0') {
        // Nothing configured yet (fresh device, or NVS cleared). Skip straight
        // to the portal instead of spending 15s waiting on a connect attempt
        // that has no SSID to attempt. This is the normal first-boot path now
        // that no build-time credentials exist.
        ESP_LOGI(TAG, "no wifi configured, starting setup portal");
        display_show("Setup needed", "Starting setup AP...");
        provisioning_start(&s_cfg);  // does not return
    }
    display_show("Connecting WiFi...", NULL);
    voice_play(VOICE_CONNECTING);
    if (!wifi_sta_wait_connected(15000)) {
        ESP_LOGW(TAG, "wifi connect failed, starting provisioning portal");
        display_show("WiFi failed", "Starting setup AP...");
        provisioning_start(&s_cfg);  // does not return
    }

    display_show("WiFi OK", "Starting…");
    ESP_ERROR_CHECK(opus_codec_init());

    // Uplink queue (mic->WS): fixed-slot pkt_t queue with off-TCB storage on both
    // targets. Storage prefers PSRAM, falls back to internal RAM on the PSRAM-less
    // C3 where MALLOC_CAP_SPIRAM returns NULL.
    static StaticQueue_t uplinkq_cb;
    const size_t q_bytes = PKT_QUEUE_DEPTH * sizeof(pkt_t);
    uint8_t *uplinkq_stor = heap_caps_malloc(q_bytes, MALLOC_CAP_SPIRAM);
    if (!uplinkq_stor) uplinkq_stor = heap_caps_malloc(q_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#if CONFIG_IDF_TARGET_ESP32C3
    // Downlink: compact NoSplit ring buffer (see dl_*), sized for a full TTS burst.
    dl_init();
    ESP_ERROR_CHECK(uplinkq_stor ? ESP_OK : ESP_ERR_NO_MEM);
#else
    // Downlink: fixed-slot pkt_t queue in PSRAM, DL_QUEUE_DEPTH deep so a whole
    // bursted TTS sentence fits (was PKT_QUEUE_DEPTH=16 -> dropped mid-sentence).
    static StaticQueue_t pktq_cb;
    const size_t dl_bytes = DL_QUEUE_DEPTH * sizeof(pkt_t);
    uint8_t *pktq_stor = heap_caps_malloc(dl_bytes, MALLOC_CAP_SPIRAM);
    if (!pktq_stor) pktq_stor = heap_caps_malloc(dl_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(pktq_stor && uplinkq_stor ? ESP_OK : ESP_ERR_NO_MEM);
    s_pktq = xQueueCreateStatic(DL_QUEUE_DEPTH, sizeof(pkt_t), pktq_stor, &pktq_cb);
#endif
    s_uplinkq = xQueueCreateStatic(PKT_QUEUE_DEPTH, sizeof(pkt_t), uplinkq_stor, &uplinkq_cb);
    s_status_q = xQueueCreate(4, sizeof(status_msg_t));
    xTaskCreatePinnedToCore(status_task, "status", 8192, NULL, 4, NULL, APP_CPU_UI);
    const esp_timer_create_args_t vol_timer_args = {
        .callback = &volume_revert_cb, .name = "vol_revert",
    };
    ESP_ERROR_CHECK(esp_timer_create(&vol_timer_args, &s_volume_revert_timer));
    // Voice-driven self.device.idle / self.audio.set_volume MCP tools reuse the
    // same transitions as the physical buttons (go_idle / show_volume_overlay
    // need s_status_q + s_volume_revert_timer, both created above).
    mcp_tools_set_idle_hook(go_idle);
    mcp_tools_set_volume_hook(show_volume_overlay);
    // Required, not optional: without it self.screen.show_text reports "screen
    // not available" rather than drawing the panel from the ws task (see
    // show_mcp_text / display_tools.c).
    mcp_tools_set_show_text_hook(show_mcp_text);
    // No hook for self.session.new: its frame is deliberately sent from the
    // LUGO_EV_MCP handler after the tool result, not from the tool itself.
    buttons_start(on_button);  // Wake toggles s_active; Vol +/- adjust volume
    // 3072 -> 6144: this task now also runs do_repair()'s blocking call chain
    // (aa_run_pairing's esp_http_client GET/POST + JSON parsing, possibly over
    // TLS when CONFIG_AA_SERVER_SECURE), not just the trivial idle-timeout
    // check it used to. Idle the rest of the time, so the extra headroom only
    // costs static RAM, not runtime.
    xTaskCreate(idle_watchdog_task, "idle_wd", 6144, NULL, 3, NULL);

    // STT/TTS/language all come from the chatllm profile server-side, and so
    // does the choice of profile itself: the gateway resolves it from this
    // device's binding (or its own defaults), so the wakeup declares none.
    // Downlink is decoded at 16 kHz to match the device opus decoder.
    // resolve_device_token(): stored NVS token -> pair now (blocks until
    // claimed; status_task is already running by this point so
    // show_pair_code's queue-based update is safe).
    const char *device_token = resolve_device_token();
    s_last_activity_s = now_s();
    ESP_ERROR_CHECK(ws_client_start(
        s_cfg.server_host, s_cfg.server_port, CONFIG_AA_SERVER_SECURE,
        device_token, 16000, 16000, 60, on_event, on_audio));

    // mic_task runs opus_encode(), which is extraordinarily stack-hungry on
    // ESP32 (SILK wideband analysis buffers live on the stack): measured
    // ~23KB peak usage via uxTaskGetStackHighWaterMark. 24576 left only ~1.2KB
    // free, so any ISR nesting on top tipped it over into the adjacent heap
    // TCB, corrupting the heap/task lists (crash surfaced elsewhere — WiFi
    // allocs, FreeRTOS tick). 40960 gives a healthy ~17KB margin.
    // spk_task runs opus_decode() (much lighter, ~4.6KB peak); uplink_task
    // owns the ws send chain.
    // On the PSRAM-less C3 internal RAM is scarce and fragments easily, so keep
    // these lean: spk_task only runs opus_decode (~4.6KB peak) and uplink_task
    // only hands opus frames to the WS client. Trimming them frees a large
    // contiguous block for mic_task's opus-encode stack (see below).
    // spk_task runs opus_decode + the jitter-buffer/state/display hand-off; 8192
    // overflowed on the C3 during TTS playback (Stack protection fault -> reboot,
    // "speaking = no sound"). 16384 is the proven size; the queue-depth cut above
    // freed the RAM to afford it.
    xTaskCreatePinnedToCore(spk_task, "spk", 16384, NULL, 6, NULL, APP_CPU_AUDIO);
    // mic_task runs opus_codec_encode inline, whose SILK-wideband analysis needs
    // a deep stack (measured 24784 B on the C3). The S3 has PSRAM/headroom for
    // 40KB; the C3 does not, so it gets a measured-fit 28KB. Created here after
    // the queues (so a big contiguous block still exists) — the 40KB S3 default
    // silently pdFAILed on the C3's fragmented heap, leaving no mic task at all
    // and a permanently silent mic.
#if CONFIG_IDF_TARGET_ESP32C3
    #define MIC_TASK_STACK 28672   /* 24784 B opus + margin */
#else
    #define MIC_TASK_STACK 40960
#endif
    // Mic/uplink priority: 5 on the S3 (dual-core, unchanged). On the single-core
    // C3, priority 5 + opus complexity 3 let the mic encode monopolize the core
    // and starve the UI/button/voice tasks (device felt hung); 4 (below spk=6,
    // above the UI tasks) leaves the core time for everything else.
#if CONFIG_IDF_TARGET_ESP32C3
    #define MIC_TASK_PRIO 4
#else
    #define MIC_TASK_PRIO 5
#endif
    if (xTaskCreatePinnedToCore(mic_task, "mic", MIC_TASK_STACK, NULL, MIC_TASK_PRIO, NULL, APP_CPU_AUDIO) != pdPASS)
        ESP_LOGE(TAG, "mic task create failed (out of internal RAM for a %d B stack)", MIC_TASK_STACK);
    xTaskCreatePinnedToCore(uplink_task, "uplink", 4096, NULL, MIC_TASK_PRIO, NULL, APP_CPU_AUDIO);

    // Connect-on-wake: the WS stays closed (asleep) until the user presses Wake.
    // No gateway connection is held while idle. Routed through s_status_q (not
    // a direct display_show call) so it's status_task — not app_main — that
    // touches the panel, same isolation rule as every other idle transition;
    // status_task is already running by this point.
#if CONFIG_AA_MIC_METER
    // Bring-up: show the mic level on the OLED, skip the gateway. peak jumps when
    // you speak => mic captures; g stays 0 => I2S RX not clocking (wiring).
    xTaskCreate(mic_meter_task, "mic_meter", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "mic meter mode (CONFIG_AA_MIC_METER)");
#elif CONFIG_AA_AUTO_WAKE
    // Dev: connect to the gateway at boot without a physical Wake button —
    // literally the BTN_WAKE (not-active) path. Remove for production.
    go_active();
    ESP_LOGI(TAG, "auto-wake: connecting to gateway (CONFIG_AA_AUTO_WAKE)");
#else
    send_idle_status("Ready");
    ESP_LOGI(TAG, "running (asleep — press wake to connect)");
#endif
}
