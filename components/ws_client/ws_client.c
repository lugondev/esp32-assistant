#include "ws_client.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ws";
static esp_websocket_client_handle_t s_client;
static ws_event_cb_t s_on_event;
static ws_audio_cb_t s_on_audio;
static volatile bool s_connected;

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base;
    esp_websocket_event_data_t *d = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true; ESP_LOGI(TAG, "connected"); break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false; ESP_LOGW(TAG, "disconnected"); break;
    case WEBSOCKET_EVENT_DATA: {
        // Only act on a complete frame delivered in a single event (no fragmentation).
        bool complete = (d->payload_offset == 0) && (d->data_len == d->payload_len);
        if (!complete) break;
        if (d->op_code == 0x02) {            // binary = one full Opus packet
            if (s_on_audio && d->data_len > 0)
                s_on_audio((const uint8_t *)d->data_ptr, d->data_len);
        } else if (d->op_code == 0x01) {     // text = one JSON event
            char buf[512];
            int n = d->data_len < (int)sizeof(buf) - 1 ? d->data_len : (int)sizeof(buf) - 1;
            memcpy(buf, d->data_ptr, n); buf[n] = '\0';
            wsp_event_t ev;
            if (wsp_parse_event(buf, &ev) == 0 && s_on_event) s_on_event(&ev);
        }
        break;
    }
    default: break;
    }
}

esp_err_t ws_client_start(const wsp_config_t *cfg,
                          ws_event_cb_t on_event, ws_audio_cb_t on_audio) {
    s_on_event = on_event; s_on_audio = on_audio;
    static char uri[512];
    if (wsp_build_uri(uri, sizeof uri, cfg) < 0) return ESP_FAIL;
    ESP_LOGI(TAG, "uri=%s", uri);
    esp_websocket_client_config_t wc = {
        .uri = uri, .reconnect_timeout_ms = 2000, .network_timeout_ms = 10000,
        .buffer_size = 2048,
    };
    s_client = esp_websocket_client_init(&wc);
    if (!s_client) return ESP_ERR_NO_MEM;
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    return esp_websocket_client_start(s_client);
}

int ws_client_send_audio(const uint8_t *opus, int len) {
    if (!s_connected) return -1;
    return esp_websocket_client_send_bin(s_client, (const char *)opus, len, portMAX_DELAY);
}

int ws_client_send_control(const char *type) {
    if (!s_connected) return -1;
    char buf[64];
    int n = wsp_build_control(buf, sizeof buf, type);
    if (n < 0) return -1;
    return esp_websocket_client_send_text(s_client, buf, n, portMAX_DELAY);
}

bool ws_client_connected(void) { return s_connected; }
