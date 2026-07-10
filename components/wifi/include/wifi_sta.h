#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_sta_start(const char *ssid, const char *password);
bool wifi_sta_wait_connected(int timeout_ms);

// Current AP RSSI in dBm, written to *out_dbm. Returns false (leaves
// *out_dbm untouched) when not currently associated.
bool wifi_sta_get_rssi(int *out_dbm);
