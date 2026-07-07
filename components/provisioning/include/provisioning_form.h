#pragma once
#include "wifi_cfg_types.h"  // struct only, no ESP-IDF deps — keeps this host-testable
#include <stddef.h>

// Renders the HTML configuration form into buf, pre-filled from `cfg` (ssid
// and server_host/server_port; password is never pre-filled, so it's never
// echoed back in page source). If `error_msg` is non-NULL and non-empty,
// renders it above the form. Returns length written (excluding NUL), or -1
// if buf is too small.
int provisioning_render_form(char *buf, size_t buflen, const wifi_cfg_t *cfg,
                              const char *error_msg);

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
