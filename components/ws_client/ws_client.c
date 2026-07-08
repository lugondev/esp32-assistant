#include "ws_client.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ws";
static esp_websocket_client_handle_t s_client;
static ws_event_cb_t s_on_event;
static ws_audio_cb_t s_on_audio;
static volatile bool s_connected;

// Wakeup handshake params, captured in ws_client_start and sent on CONNECTED.
static char s_profile[64];
static int  s_in_sr, s_out_sr, s_frame_ms;

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base;
    esp_websocket_event_data_t *d = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED: {
        s_connected = true;
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
    case WEBSOCKET_EVENT_ERROR:
        s_connected = false; ESP_LOGW(TAG, "ws error"); break;
    case WEBSOCKET_EVENT_DATA: {
        // Only act on a complete frame delivered in a single event.
        bool complete = (d->payload_offset == 0) && (d->data_len == d->payload_len);
        if (!complete) break;
        if (d->op_code == 0x02) {            // binary = v3 frame (header + opus)
            uint8_t type; const uint8_t *payload; int plen;
            if (lugo_frame_decode((const uint8_t *)d->data_ptr, d->data_len,
                                  &type, &payload, &plen) == 0 &&
                type == LUGO_FRAME_OPUS && s_on_audio && plen > 0) {
                s_on_audio(payload, plen);
            }
        } else if (d->op_code == 0x01) {     // text = one Lugo JSON event
            char buf[512];
            int n = d->data_len < (int)sizeof(buf) - 1 ? d->data_len : (int)sizeof(buf) - 1;
            memcpy(buf, d->data_ptr, n); buf[n] = '\0';
            lugo_event_t ev;
            if (lugo_parse_event(buf, &ev) == 0 && s_on_event) s_on_event(&ev);
        }
        break;
    }
    default: break;
    }
}

// See original rationale: a graceful server CLOSE leaves the built-in reconnect
// stopped, so we disable it and drive one uniform reconnect here.
static void ws_reconnect_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        if (s_client && !esp_websocket_client_is_connected(s_client)) {
            ESP_LOGW(TAG, "link down — forcing reconnect");
            esp_websocket_client_stop(s_client);
            esp_websocket_client_start(s_client);
        }
    }
}

esp_err_t ws_client_start(const char *host, int port, bool secure,
                          const char *profile, int in_sr, int out_sr, int frame_ms,
                          ws_event_cb_t on_event, ws_audio_cb_t on_audio) {
    s_on_event = on_event; s_on_audio = on_audio;
    strncpy(s_profile, profile ? profile : "", sizeof(s_profile) - 1);
    s_in_sr = in_sr; s_out_sr = out_sr; s_frame_ms = frame_ms;

    static char uri[512];
    int n = snprintf(uri, sizeof uri, "%s://%s:%d/v1/lugo/stream",
                     secure ? "wss" : "ws", host, port);
    if (n < 0 || n >= (int)sizeof uri) return ESP_FAIL;
    ESP_LOGI(TAG, "uri=%s", uri);

    esp_websocket_client_config_t wc = {
        .uri = uri, .network_timeout_ms = 10000,
        .disable_auto_reconnect = true,  // ws_reconnect_task drives reconnects
        .buffer_size = 2048,
        .task_stack = 8192,  // on_event() calls into display/i2s on this task
    };
    s_client = esp_websocket_client_init(&wc);
    if (!s_client) return ESP_ERR_NO_MEM;
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    esp_err_t err = esp_websocket_client_start(s_client);
    xTaskCreate(ws_reconnect_task, "ws_reconn", 3072, NULL, 3, NULL);
    return err;
}

int ws_client_send_audio(const uint8_t *opus, int len) {
    if (!s_connected) return -1;
    // Uplink is RAW opus: the gateway feeds inbound binary straight to the STT
    // opus decoder and does not strip a v3 header.
    return esp_websocket_client_send_bin(s_client, (const char *)opus, len, portMAX_DELAY);
}

int ws_client_send_abort(const char *reason) {
    if (!s_connected) return -1;
    char buf[64];
    int n = lugo_build_abort(buf, sizeof buf, reason);
    if (n < 0) return -1;
    return esp_websocket_client_send_text(s_client, buf, n, portMAX_DELAY);
}

bool ws_client_connected(void) { return s_connected; }
