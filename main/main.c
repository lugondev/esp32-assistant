#include "wifi_sta.h"
#include "esp_log.h"

static const char *TAG = "app";

void app_main(void) {
    ESP_LOGI(TAG, "esp32-assistant booting");
    ESP_ERROR_CHECK(wifi_sta_start());
    if (wifi_sta_wait_connected(20000)) {
        ESP_LOGI(TAG, "WiFi connected");
    } else {
        ESP_LOGE(TAG, "WiFi connect timeout");
    }
}
