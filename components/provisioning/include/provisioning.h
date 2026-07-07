#pragma once
#include "wifi_cfg.h"

// Brings up SoftAP "Lugo-XXXX" (open, 192.168.9.1) + a captive DNS responder
// + an HTTP config portal pre-filled from `current`. Blocks the calling task
// forever. On successful form submission it saves the new config to NVS and
// calls esp_restart() (does not return in that case either). Assumes
// esp_netif_init()/esp_event_loop_create_default() and esp_wifi_init() have
// already run (true whenever called from app_main after wifi_sta_start()).
void provisioning_start(const wifi_cfg_t *current);
