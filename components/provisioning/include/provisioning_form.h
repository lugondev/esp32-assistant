#pragma once
#include "wifi_cfg_types.h"  // struct only, no ESP-IDF deps — keeps this host-testable
#include <stdbool.h>
#include <stddef.h>

// One entry of the WiFi scan shown in the portal's network picker. Filled on
// the device from wifi_ap_record_t (provisioning.c), but declared here with no
// ESP-IDF types so the whole rendering path stays host-testable.
typedef struct {
    char ssid[WIFI_CFG_SSID_MAX + 1];
    int  rssi;     // dBm, e.g. -47
    bool secure;   // true unless the AP is open
} prov_network_t;

// Upper bound on networks kept from a scan. A busy apartment block returns 40+
// APs; past ~20 the list is unusable on a phone and each extra row costs page
// bytes, so the scan is sorted strongest-first and truncated here.
#define PROV_MAX_NETWORKS 20

// Byte budget for a rendered page. The form is a styled document plus one row
// per network, so it no longer fits the 4 KB the plain-text version used:
// ~5 KB of markup/CSS/JS + ~250 B per row once SSIDs are escaped.
//
// Measured worst case — PROV_MAX_NETWORKS rows of 32-char SSIDs, plus an error
// banner — is ~10.6 KB (test_render_full_list_fits_page_buffer prints it on
// every run). 16 KB keeps ~35% headroom, because overflowing this is not a
// clipped page: render returns -1 and the portal serves an HTTP 500, i.e. the
// device becomes unconfigurable. The buffer is a transient per-request malloc,
// so the headroom costs nothing at rest.
#define PROV_PAGE_BUF 16384

// Signal strength for the scan list comes from wifi_signal_bars()
// (components/wifi/include/wifi_signal.h) — the same ladder the device's own
// status bar reads, so the phone and the panel agree. This header used to
// declare a provisioning_signal_bars() that was a second copy of it.

// De-duplicates by SSID (the same network usually answers on both bands, and
// mesh setups repeat it per node — the strongest sighting of each wins) and
// sorts strongest-first, in place. Drops entries with an empty SSID (hidden
// networks broadcast a blank one and can't be picked off a list anyway).
// Returns the new count, capped at PROV_MAX_NETWORKS.
int provisioning_sort_networks(prov_network_t *nets, int n);

// Renders the HTML configuration page into buf, pre-filled from `cfg` (ssid
// and server_host/server_port; password is never pre-filled, so it's never
// echoed back in page source). If `error_msg` is non-NULL and non-empty,
// renders it above the form. `nets`/`n_nets` supply the tappable network
// picker; pass NULL/0 to render manual entry only (e.g. when the scan found
// nothing). Returns length written (excluding NUL), or -1 if buf is too small.
int provisioning_render_form(char *buf, size_t buflen, const wifi_cfg_t *cfg,
                              const char *error_msg,
                              const prov_network_t *nets, int n_nets);

// Renders the short "saved, restarting" confirmation page. Returns length
// written (excluding NUL), or -1 if buf is too small.
int provisioning_render_saved(char *buf, size_t buflen);

// Parses an application/x-www-form-urlencoded POST body (ssid, password,
// host, port fields) into *out. Percent-decodes and '+'-decodes values.
// Returns 0 on success. Returns -1 if ssid is missing/empty, host is
// missing/empty, or port is missing, non-numeric, or not in [1, 65535]. If
// the password field is absent or empty, out->password is set to "" (caller
// decides whether to preserve a previously-saved password in that case).
int provisioning_parse_form(const char *body, size_t len, wifi_cfg_t *out);
