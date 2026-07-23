#include "pairing.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#define NS  "lugo"
#define KEY "device_token"

int aa_load_device_token(char *out, int cap) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t len = (size_t) cap;
    esp_err_t err = nvs_get_str(h, KEY, out, &len);
    nvs_close(h);
    if (err == ESP_OK) return (int) strlen(out);
    if (err == ESP_ERR_NVS_NOT_FOUND) return 0;
    return -1;
}

int aa_save_device_token(const char *token) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_set_str(h, KEY, token);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK ? 0 : -1;
}

int aa_clear_device_token(void) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t err = nvs_erase_key(h, KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;  // idempotent
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK ? 0 : -1;
}
