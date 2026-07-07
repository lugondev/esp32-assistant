#include "provisioning_ssid.h"
#include <stdio.h>

int provisioning_build_ssid(const uint8_t mac[6], char *buf, size_t buflen) {
    int n = snprintf(buf, buflen, "Lugo-%02X%02X", mac[4], mac[5]);
    if (n < 0 || (size_t)n >= buflen) return -1;
    return n;
}
