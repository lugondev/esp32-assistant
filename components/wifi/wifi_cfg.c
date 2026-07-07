#include "wifi_cfg.h"
#include "nvs.h"
#include <string.h>

#define NVS_NS "aa_cfg"

static esp_err_t load_str(nvs_handle_t h, const char *key, char *out,
                           size_t outlen, const char *fallback) {
    size_t len = outlen;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(out, fallback, outlen - 1);
        out[outlen - 1] = '\0';
        return ESP_OK;
    }
    return err;
}

esp_err_t wifi_cfg_load(wifi_cfg_t *out) {
    memset(out, 0, sizeof(*out));
    strncpy(out->server_host, CONFIG_AA_SERVER_HOST, sizeof(out->server_host) - 1);
    out->server_port = CONFIG_AA_SERVER_PORT;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;  // namespace never created yet: keep the defaults above
    }
    if (err != ESP_OK) return err;

    err = load_str(h, "ssid", out->ssid, sizeof(out->ssid), "");
    if (err != ESP_OK) { nvs_close(h); return err; }
    err = load_str(h, "pass", out->password, sizeof(out->password), "");
    if (err != ESP_OK) { nvs_close(h); return err; }
    err = load_str(h, "host", out->server_host, sizeof(out->server_host),
                   CONFIG_AA_SERVER_HOST);
    if (err != ESP_OK) { nvs_close(h); return err; }

    int32_t port = 0;
    err = nvs_get_i32(h, "port", &port);
    if (err == ESP_OK) {
        out->server_port = (int)port;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return err;
    }

    nvs_close(h);
    return ESP_OK;
}

esp_err_t wifi_cfg_save(const wifi_cfg_t *cfg) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, "ssid", cfg->ssid);
    if (err == ESP_OK) err = nvs_set_str(h, "pass", cfg->password);
    if (err == ESP_OK) err = nvs_set_str(h, "host", cfg->server_host);
    if (err == ESP_OK) err = nvs_set_i32(h, "port", cfg->server_port);
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    return err;
}
