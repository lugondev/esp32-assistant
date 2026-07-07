#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_sta_start(const char *ssid, const char *password);
bool wifi_sta_wait_connected(int timeout_ms);
