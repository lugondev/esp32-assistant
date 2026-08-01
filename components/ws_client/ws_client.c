#include "ws_client.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ws";
// Receive buffer: any single inbound frame (opus packet / JSON event / MCP
// request) must fit or it arrives fragmented and is dropped (see on_ws).
#define WS_BUF_SIZE 2048
static esp_websocket_client_handle_t s_client;
static ws_event_cb_t s_on_event;
static ws_audio_cb_t s_on_audio;
static volatile bool s_connected;
// Sleep-until-wake: when false, the reconnect task leaves the socket closed.
// Set false on a server idle `goodbye` so the device sleeps instead of
// reconnect-storming every idle_timeout; set true again when the user wakes.
static volatile bool s_reconnect_enabled = true;

// Wakeup handshake params, captured in ws_client_start and sent on CONNECTED.
static char s_profile[64];
static int  s_in_sr, s_out_sr, s_frame_ms;

// HTTP status of a rejected WS handshake since the last CONNECTED (e.g. 403
// when the device_token was revoked). Reset on CONNECTED, populated from
// esp_ws_handshake_status_code on ERROR when the underlying lib provides it.
static int s_last_handshake_status = 0;

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base;
    esp_websocket_event_data_t *d = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED: {
        s_connected = true;
        s_last_handshake_status = 0;
        ESP_LOGI(TAG, "connected");
        char buf[256];
        int n = lugo_build_wakeup(buf, sizeof buf, s_profile, s_in_sr, s_out_sr, s_frame_ms);
        if (n > 0) esp_websocket_client_send_text(s_client, buf, n, portMAX_DELAY);
        break;
    }
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false; ESP_LOGW(TAG, "disconnected"); break;
    // A server-initiated graceful close arrives as CLOSED, not DISCONNECTED.
    case WEBSOCKET_EVENT_CLOSED:
        s_connected = false; ESP_LOGW(TAG, "closed"); break;
    case WEBSOCKET_EVENT_ERROR: {
        // The vendored esp_websocket_client sets error_handle's handshake
        // status once (on a failed transport connect) and never clears it,
        // then copies it into every subsequent ERROR event verbatim. A real
        // handshake rejection can only happen before this attempt's
        // CONNECTED fires; capture s_connected (about to be cleared) before
        // clobbering it so a later mid-session error (e.g. a Wi-Fi blip)
        // doesn't re-report a stale status from an earlier, unrelated
        // connect attempt as a fresh rejection (genuine mid-session revoke
        // is a `goodbye reason=account_disabled`, not a handshake status).
        bool was_established = s_connected;
        s_connected = false;
        if (!was_established && d->error_handle.esp_ws_handshake_status_code > 0)
            s_last_handshake_status = d->error_handle.esp_ws_handshake_status_code;
        ESP_LOGW(TAG, "ws error");
        break;
    }
    case WEBSOCKET_EVENT_DATA: {
        // Only act on a complete frame delivered in a single event. Frames
        // larger than WS_BUF_SIZE arrive fragmented and are dropped — say so
        // (once, on the first fragment) instead of failing silently.
        bool complete = (d->payload_offset == 0) && (d->data_len == d->payload_len);
        if (!complete) {
            if (d->payload_offset == 0)
                ESP_LOGW(TAG, "dropping fragmented ws frame (%d bytes > %d buffer)",
                         d->payload_len, WS_BUF_SIZE);
            break;
        }
        if (d->op_code == 0x02) {            // binary = v3 frame (header + opus)
            uint8_t type; const uint8_t *payload; int plen;
            if (lugo_frame_decode((const uint8_t *)d->data_ptr, d->data_len,
                                  &type, &payload, &plen) == 0 &&
                type == LUGO_FRAME_OPUS && s_on_audio && plen > 0) {
                s_on_audio(payload, plen);
            }
        } else if (d->op_code == 0x01) {     // text = one Lugo JSON event
            // Sized at WS_BUF_SIZE, not the 512 it used to be: anything larger
            // than WS_BUF_SIZE already arrives fragmented and is rejected
            // above, so this now covers every frame that can reach here and
            // silent truncation is impossible. At 512 the whole 512..2048 band
            // was memcpy'd in half and parsed as if complete — a long stt
            // transcript came through cut, and an mcp tools/call whose params
            // straddled the cut simply became "unknown tool" with nothing in
            // the log to explain it (outbound mcp frames get 3072 bytes, so
            // the two directions were 6x apart).
            //
            // static, not on the stack: the ws task's 8 KB also carries
            // on_event()'s whole call chain. Safe because esp_websocket_client
            // dispatches every event from its own single task.
            static char buf[WS_BUF_SIZE + 1];
            int n = d->data_len;
            if (n > (int)sizeof(buf) - 1) {   // unreachable via the check above; fail loudly if it ever isn't
                ESP_LOGW(TAG, "text frame %d B exceeds %d B buffer — truncating", n, (int)sizeof(buf) - 1);
                n = (int)sizeof(buf) - 1;
            }
            memcpy(buf, d->data_ptr, n); buf[n] = '\0';
            lugo_event_t ev;
            if (lugo_parse_event(buf, &ev) == 0 && s_on_event) s_on_event(&ev);
        }
        break;
    }
    default: break;
    }
}

static volatile bool s_wake_pending = false;  // one-shot: connect NOW on wake

// Connect-on-wake lifecycle manager (all stop/start happens here, never from a
// caller's task context). Behaviour by state:
//   - asleep (idle / after goodbye): keep the socket closed; if it's still open
//     (manual idle), close it.
//   - just woken (s_wake_pending): connect immediately for a snappy wake.
//   - awake but link dropped unexpectedly: reconnect, throttled to ~3s so we
//     don't restart an in-flight connect attempt.
static void ws_conn_task(void *arg) {
    (void)arg;
    int down_ms = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        bool connected = s_client && esp_websocket_client_is_connected(s_client);
        if (!s_reconnect_enabled) {                 // asleep
            if (connected) { ESP_LOGI(TAG, "sleep: closing link"); esp_websocket_client_stop(s_client); }
            s_wake_pending = false;
            down_ms = 0;
            continue;
        }
        if (connected) { down_ms = 0; continue; }
        if (s_wake_pending) {                        // wake -> connect immediately
            s_wake_pending = false;
            down_ms = 0;
            ESP_LOGI(TAG, "wake: connecting");
            esp_websocket_client_stop(s_client);     // ensure a clean start
            esp_websocket_client_start(s_client);
            continue;
        }
        down_ms += 250;                              // unexpected drop while awake:
        if (down_ms >= 3000) {                       // throttled reconnect
            down_ms = 0;
            ESP_LOGW(TAG, "link down — reconnecting");
            esp_websocket_client_stop(s_client);
            esp_websocket_client_start(s_client);
        }
    }
}

// Sleep (false) closes/keeps the link closed until the next wake; wake (true)
// requests an immediate connect. Only touches flags — ws_conn_task does the work.
void ws_client_set_reconnect(bool enabled) {
    s_reconnect_enabled = enabled;
    if (enabled) s_wake_pending = true;
}

esp_err_t ws_client_start(const char *host, int port, bool secure,
                          const char *profile, const char *device_token,
                          int in_sr, int out_sr, int frame_ms,
                          ws_event_cb_t on_event, ws_audio_cb_t on_audio) {
    s_on_event = on_event; s_on_audio = on_audio;
    strncpy(s_profile, profile ? profile : "", sizeof(s_profile) - 1);
    s_in_sr = in_sr; s_out_sr = out_sr; s_frame_ms = frame_ms;

    static char uri[512];
    // device_token is a plain alnum/-/_ secret (base64url-style), so no URL
    // escaping is needed in the query param.
    int n = (device_token && device_token[0])
        ? snprintf(uri, sizeof uri, "%s://%s:%d/v1/lugo/stream?device_token=%s",
                   secure ? "wss" : "ws", host, port, device_token)
        : snprintf(uri, sizeof uri, "%s://%s:%d/v1/lugo/stream",
                   secure ? "wss" : "ws", host, port);
    if (n < 0 || n >= (int)sizeof uri) return ESP_FAIL;
    // Never log the token itself (goes to the serial console).
    ESP_LOGI(TAG, "uri=%s://%s:%d/v1/lugo/stream%s", secure ? "wss" : "ws", host, port,
             (device_token && device_token[0]) ? "?device_token=<redacted>" : "");

    esp_websocket_client_config_t wc = {
        .uri = uri, .network_timeout_ms = 10000,
        .disable_auto_reconnect = true,  // ws_conn_task drives connect/disconnect
        .buffer_size = WS_BUF_SIZE,
        .task_stack = 8192,  // on_event() runs the whole lugo/mcp dispatch here
        // wss:// needs a CA to verify the server against, and this client had
        // none — so CONFIG_AA_SERVER_SECURE=y built a wss:// URI that could
        // never complete a handshake. The bundle is already compiled in
        // (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE), it just has to be attached.
        // NULL for ws://, where esp-tls is not in the path at all.
        .crt_bundle_attach = secure ? esp_crt_bundle_attach : NULL,
    };
    s_client = esp_websocket_client_init(&wc);
    if (!s_client) return ESP_ERR_NO_MEM;
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    // Connect-on-wake: start asleep (WS closed). ws_conn_task connects on the
    // first ws_client_set_reconnect(true) — i.e. when the user wakes the device.
    s_reconnect_enabled = false;
    xTaskCreate(ws_conn_task, "ws_conn", 3072, NULL, 3, NULL);
    return ESP_OK;
}

int ws_client_send_audio(const uint8_t *opus, int len) {
    if (!s_connected) return -1;
    // Uplink is RAW opus: the gateway feeds inbound binary straight to the STT
    // opus decoder and does not strip a v3 header.
    //
    // portMAX_DELAY is deliberate, despite this being a real-time path where a
    // bounded timeout looks like the obvious choice. A finite timeout here does
    // NOT mean "give up on this frame": esp_transport_ws.c's _ws_write() polls
    // the socket for writability first and returns 0 on timeout, and
    // esp_websocket_client.c treats a 0-length write as a transport failure and
    // calls esp_websocket_client_abort_connection(). So a momentary Wi-Fi stall
    // would tear the WebSocket down and force a full reconnect (with no
    // `goodbye`, which also feeds the disconnect classifier) instead of costing
    // one frame. Blocking is the cheaper failure.
    // The backlog that builds up while this blocks is handled where it can be
    // handled correctly — mic_task drops the stale queue and resyncs.
    return esp_websocket_client_send_bin(s_client, (const char *)opus, len, portMAX_DELAY);
}

int ws_client_send_abort(const char *reason) {
    if (!s_connected) return -1;
    char buf[64];
    int n = lugo_build_abort(buf, sizeof buf, reason);
    if (n < 0) return -1;
    return esp_websocket_client_send_text(s_client, buf, n, portMAX_DELAY);
}

int ws_client_send_new_session(void) {
    if (!s_connected) return -1;
    char buf[48];
    int n = lugo_build_new_session(buf, sizeof buf);
    if (n < 0) return -1;
    return esp_websocket_client_send_text(s_client, buf, n, portMAX_DELAY);
}

int ws_client_send_mcp(const char *json_payload) {
    if (!s_connected) return -1;
    static char buf[MCP_FRAME_BUF_SIZE];
    int n = snprintf(buf, sizeof buf, "{\"type\":\"mcp\",\"payload\":%s}", json_payload);
    if (n < 0 || n >= (int)sizeof buf) return -1;
    return esp_websocket_client_send_text(s_client, buf, n, portMAX_DELAY);
}

bool ws_client_connected(void) { return s_connected; }

int ws_client_last_handshake_status(void) { return s_last_handshake_status; }
