#pragma once
#include <stdint.h>
#include <stddef.h>

// Builds AP SSID "Lugo-XXXX" where XXXX is the last 2 bytes of `mac` (6 bytes)
// as uppercase hex, e.g. {..,0x48,0xD0} -> "Lugo-48D0". Stable across reboots
// (derived from the device's own MAC, not randomized). Returns length written
// (excluding NUL), or -1 if buf is too small.
int provisioning_build_ssid(const uint8_t mac[6], char *buf, size_t buflen);
