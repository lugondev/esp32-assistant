#pragma once
#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_sta_start(void);
bool wifi_sta_wait_connected(int timeout_ms);
