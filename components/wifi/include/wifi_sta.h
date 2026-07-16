#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_sta_start(const char *ssid, const char *password);
bool wifi_sta_wait_connected(int timeout_ms);

// Current AP RSSI in dBm, written to *out_dbm. Returns false (leaves
// *out_dbm untouched) when not currently associated.
bool wifi_sta_get_rssi(int *out_dbm);

// Latency/power trade for the WiFi modem: perf=true disables modem
// power-save for the duration of a conversation (the default MIN_MODEM doze
// adds tens of ms of jitter per beacon interval to the audio stream);
// perf=false restores power-save for idle. Safe to call redundantly.
void wifi_sta_set_perf_mode(bool perf);
