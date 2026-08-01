#include "pairing.h"
#include "lugo_protocol.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "pairing";

// JSON parsing is lugo_protocol's job (lugo_json_get_string_strict), not this
// file's. There used to be a hand-rolled extract_str() here and a second,
// subtly different hand-rolled parser in pairing_logic.c — three parsers in the
// firmware for one JSON dialect, each with its own idea of which whitespace is
// legal after a colon. The strict getter keeps the property this code depends
// on: a value too long for its buffer is an ERROR, never a silent truncation
// (see AA_PAIR_CODE_MAX in pairing.h for what that cost last time).

// Minimal blocking GET/POST into a fixed buffer. Returns HTTP status, or <0 on transport error.
static int http_call(const char *url, const char *method, const char *body,
                     char *resp, int resp_cap) {
    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = 30000,
        // Attach the compiled-in CA bundle so an https:// base_url (i.e.
        // CONFIG_AA_SERVER_SECURE=y) can actually verify the gateway. Without
        // it esp-tls has no trust anchor and every pairing request failed the
        // handshake, so the secure build could never claim a token at all.
        // Harmless on plain http://, where the TLS stack is never entered.
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;
    esp_http_client_set_method(c, strcmp(method, "POST") == 0
                                  ? HTTP_METHOD_POST : HTTP_METHOD_GET);
    if (body) {
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_post_field(c, body, strlen(body));
    }
    int status = -1;
    if (esp_http_client_open(c, body ? (int) strlen(body) : 0) == ESP_OK) {
        if (body) esp_http_client_write(c, body, strlen(body));
        esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        int n = esp_http_client_read_response(c, resp, resp_cap - 1);
        resp[n > 0 ? n : 0] = '\0';
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return status;
}

int aa_run_pairing(const char *base_url, const char *serial,
                   aa_show_code_fn show, char *token_out, int token_cap) {
    char url[256], body[96], resp[512], code[AA_PAIR_CODE_MAX], poll_token[128];
    for (;;) {
        snprintf(url, sizeof url, "%s/v1/devices/pair/init", base_url);
        snprintf(body, sizeof body, "{\"serial\":\"%s\"}", serial);
        int st = http_call(url, "POST", body, resp, sizeof resp);
        if (st != 200) { ESP_LOGW(TAG, "pair/init http %d", st); vTaskDelay(pdMS_TO_TICKS(3000)); continue; }
        if (lugo_json_get_string_strict(resp, "code", code, sizeof code) != 0 ||
            lugo_json_get_string_strict(resp, "poll_token", poll_token, sizeof poll_token) != 0) {
            ESP_LOGW(TAG, "pair/init parse failed"); vTaskDelay(pdMS_TO_TICKS(3000)); continue;
        }
        ESP_LOGI(TAG, "pairing code: %s", code);   // code is safe to log; token is not
        if (show) show(code);

        for (;;) {
            snprintf(url, sizeof url, "%s/v1/devices/pair/status?poll_token=%s", base_url, poll_token);
            st = http_call(url, "GET", NULL, resp, sizeof resp);
            if (st == 404) break;                    // expired -> re-init, fresh code
            if (st == 200) {
                int r = aa_parse_pair_status(resp, token_out, token_cap);
                if (r == 1) { aa_save_device_token(token_out); return 0; }
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
}
