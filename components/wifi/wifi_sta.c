#include "wifi_sta.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi";
static EventGroupHandle_t s_events;
#define BIT_CONNECTED BIT0

// Reconnect backoff: an AP that's actually gone (power cut, out of range)
// used to trigger an immediate esp_wifi_connect() on every DISCONNECTED
// event — a tight scan/associate loop that burns power and airtime for as
// long as the outage lasts. Back off 500ms -> 8s instead; reset on got-IP.
static esp_timer_handle_t s_reconnect_timer;
static int s_disconnects;   // consecutive failures since the last got-IP

static void reconnect_timer_cb(void *arg) {
    (void)arg;
    esp_wifi_connect();
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, BIT_CONNECTED);
        int shift = s_disconnects < 4 ? s_disconnects : 4;
        if (s_disconnects < 4) s_disconnects++;
        uint64_t delay_ms = 500ULL << shift;   // 500ms, 1s, 2s, 4s, 8s cap
        ESP_LOGW(TAG, "disconnected; reconnecting in %ums", (unsigned)delay_ms);
        esp_timer_stop(s_reconnect_timer);   // no-op if not running
        esp_timer_start_once(s_reconnect_timer, delay_ms * 1000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "got IP");
        s_disconnects = 0;
        xEventGroupSetBits(s_events, BIT_CONNECTED);
    }
}

esp_err_t wifi_sta_start(const char *ssid, const char *password) {
    s_events = xEventGroupCreate();
    const esp_timer_create_args_t rt_args = {
        .callback = &reconnect_timer_cb, .name = "wifi_reconn",
    };
    ESP_ERROR_CHECK(esp_timer_create(&rt_args, &s_reconnect_timer));
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, password, sizeof(wc.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

bool wifi_sta_wait_connected(int timeout_ms) {
    EventBits_t bits = xEventGroupWaitBits(
        s_events, BIT_CONNECTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & BIT_CONNECTED) != 0;
}

bool wifi_sta_get_rssi(int *out_dbm) {
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK) return false;   // not associated
    *out_dbm = info.rssi;
    return true;
}

void wifi_sta_set_perf_mode(bool perf) {
    esp_wifi_set_ps(perf ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
}
