#pragma once
#include "wifi_cfg_types.h"
#include "esp_err.h"

// Loads saved WiFi/gateway config from NVS (namespace "aa_cfg"). ssid/password
// are "" if never saved (first boot -> caller should provision). server_host/
// server_port fall back to CONFIG_AA_SERVER_HOST/CONFIG_AA_SERVER_PORT when
// not yet saved in NVS. Requires nvs_flash_init() to have already run.
esp_err_t wifi_cfg_load(wifi_cfg_t *out);

// Persists cfg to NVS (namespace "aa_cfg"). Commits before returning.
esp_err_t wifi_cfg_save(const wifi_cfg_t *cfg);
