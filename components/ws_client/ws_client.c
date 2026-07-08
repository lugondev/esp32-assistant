#include "ws_client.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
    // A server-initiated graceful close arrives as CLOSED, not DISCONNECTED.
    // Without this the flag stayed true after the gateway ended the session, so
    // mic_task kept streaming into a dead socket, flooding "client not
    // connected" errors. ERROR likewise means the link is unusable. Clearing
    // the flag stops the uplink and lets auto-reconnect re-establish cleanly.
    case WEBSOCKET_EVENT_CLOSED:
        s_connected = false; ESP_LOGW(TAG, "closed"); break;
    case WEBSOCKET_EVENT_ERROR:
        s_connected = false; ESP_LOGW(TAG, "ws error"); break;
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

// The built-in auto-reconnect only fires after an abrupt DISCONNECTED; a
// graceful server CLOSE frame (the gateway ends the session or restarts) leaves
// the client stopped, so the device sat dead until a manual reboot. We disable
// the built-in reconnect and drive one uniform reconnect here: whenever the
// link is down, stop+start forces a fresh connection (a new session), covering
// both close and disconnect the same way.
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

esp_err_t ws_client_start(const wsp_config_t *cfg,
                          ws_event_cb_t on_event, ws_audio_cb_t on_audio) {
    s_on_event = on_event; s_on_audio = on_audio;
    static char uri[512];
    if (wsp_build_uri(uri, sizeof uri, cfg) < 0) return ESP_FAIL;
    ESP_LOGI(TAG, "uri=%s", uri);
    esp_websocket_client_config_t wc = {
        .uri = uri, .network_timeout_ms = 10000,
        .disable_auto_reconnect = true,  // ws_reconnect_task drives reconnects
        .buffer_size = 2048,
        // Default (4KB, WEBSOCKET_TASK_STACK) overflowed once on_event()
        // started calling into display_show()/voice_play() (SPI/I2S driver
        // calls), which run on this same task via the WS event callback.
        .task_stack = 8192,
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
