#pragma once
#include "wifi_cfg.h"

// Brings up SoftAP "Lugo-XXXX" (open, 192.168.9.1) + a captive DNS responder
// + an HTTP config portal pre-filled from `current`. Blocks the calling task
// forever. On successful form submission it saves the new config to NVS and
// calls esp_restart() (does not return in that case either). Assumes
// esp_netif_init()/esp_event_loop_create_default() and esp_wifi_init() have
// already run (true whenever called from app_main after wifi_sta_start()).
void provisioning_start(const wifi_cfg_t *current);

// Redraws the portal's "Setup WiFi / Lugo-XXXX 192.168.9.1" screen. A no-op if
// the portal was never started, so callers don't need to know whether it is up.
// Exists because the panel is shared: the erase-confirmation prompt can be
// raised from the button task while the portal is running, and cancelling it
// must not leave the user without the address to connect to.
void provisioning_redraw_status(void);
