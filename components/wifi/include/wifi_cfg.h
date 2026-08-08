#pragma once
#include "wifi_cfg_types.h"
#include "esp_err.h"
#include <stdbool.h>

// Loads saved WiFi/gateway config from NVS (namespace "aa_cfg"). ssid/password
// are "" if never saved (first boot -> caller should provision). server_host/
// server_port fall back to CONFIG_AA_SERVER_HOST/CONFIG_AA_SERVER_PORT when
// not yet saved in NVS. Requires nvs_flash_init() to have already run.
esp_err_t wifi_cfg_load(wifi_cfg_t *out);

// Persists cfg to NVS (namespace "aa_cfg"). Commits before returning.
esp_err_t wifi_cfg_save(const wifi_cfg_t *cfg);

// Requests the setup portal be shown on next boot, regardless of what's
// saved in ssid/password/server_host/server_port. One-shot: cleared the
// next time wifi_cfg_take_setup_request() is called. Call this before
// esp_restart() — it does not restart on its own.
esp_err_t wifi_cfg_request_setup(void);

// Returns true (and clears the flag) if wifi_cfg_request_setup() was
// called before the last reboot. Call once at boot, before deciding
// whether to skip straight to the setup portal. Returns false, not an
// error, if the flag was never set or the "aa_cfg" namespace doesn't
// exist yet.
bool wifi_cfg_take_setup_request(void);
