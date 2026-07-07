#pragma once

#define WIFI_CFG_SSID_MAX 32
#define WIFI_CFG_PASS_MAX 64
#define WIFI_CFG_HOST_MAX 127

typedef struct {
    char ssid[WIFI_CFG_SSID_MAX + 1];
    char password[WIFI_CFG_PASS_MAX + 1];
    char server_host[WIFI_CFG_HOST_MAX + 1];
    int  server_port;
} wifi_cfg_t;
