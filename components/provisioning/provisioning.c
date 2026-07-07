#include "provisioning.h"
#include "provisioning_ssid.h"
#include "provisioning_form.h"
#include "display.h"
#include "voice.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/ip4_addr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "provisioning";

static wifi_cfg_t s_cfg;  // working copy shown/edited by the portal

static void dns_task(void *arg) {
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "dns socket failed"); vTaskDelete(NULL); return; }

    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
        ESP_LOGE(TAG, "dns bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t req[512];
    uint8_t resp[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof from;
        int len = recvfrom(sock, req, sizeof req, 0, (struct sockaddr *)&from, &fromlen);
        if (len < 12) continue;  // shorter than a DNS header

        int qend = 12;
        while (qend < len && req[qend] != 0) qend += req[qend] + 1;
        qend += 1 + 4;  // zero label + QTYPE(2) + QCLASS(2)
        if (qend > len || qend + 16 > (int)sizeof resp) continue;

        memcpy(resp, req, qend);
        resp[2] = 0x81; resp[3] = 0x80;   // QR=1, RA=1
        resp[6] = 0; resp[7] = 1;         // ANCOUNT = 1
        resp[8] = 0; resp[9] = 0;         // NSCOUNT = 0
        resp[10] = 0; resp[11] = 0;       // ARCOUNT = 0

        int p = qend;
        resp[p++] = 0xC0; resp[p++] = 0x0C;              // name = pointer to offset 12
        resp[p++] = 0x00; resp[p++] = 0x01;              // TYPE = A
        resp[p++] = 0x00; resp[p++] = 0x01;              // CLASS = IN
        resp[p++] = 0x00; resp[p++] = 0x00;
        resp[p++] = 0x00; resp[p++] = 0x3C;              // TTL = 60
        resp[p++] = 0x00; resp[p++] = 0x04;               // RDLENGTH = 4
        resp[p++] = 192; resp[p++] = 168; resp[p++] = 9; resp[p++] = 1;  // 192.168.9.1

        sendto(sock, resp, p, 0, (struct sockaddr *)&from, fromlen);
    }
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    char *buf = malloc(4096);
    if (!buf) return ESP_ERR_NO_MEM;
    int n = provisioning_render_form(buf, 4096, &s_cfg, NULL);
    if (n < 0) { free(buf); httpd_resp_send_500(req); return ESP_FAIL; }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, n);
    free(buf);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len > 2048) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }
    char *body = malloc(req->content_len + 1);
    if (!body) return ESP_ERR_NO_MEM;
    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) { free(body); httpd_resp_send_500(req); return ESP_FAIL; }
        received += r;
    }
    body[received] = '\0';

    wifi_cfg_t parsed;
    memset(&parsed, 0, sizeof parsed);
    int rc = provisioning_parse_form(body, received, &parsed);
    free(body);

    char *resp_buf = malloc(4096);
    if (!resp_buf) return ESP_ERR_NO_MEM;

    if (rc != 0) {
        int n = provisioning_render_form(resp_buf, 4096, &s_cfg,
            "Invalid input: SSID and gateway host are required, port must be 1-65535.");
        if (n < 0) { free(resp_buf); httpd_resp_send_500(req); return ESP_FAIL; }
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, resp_buf, n);
        free(resp_buf);
        return ESP_OK;
    }

    // The form never pre-fills the password field; if left blank, keep the
    // previously-saved one instead of wiping it.
    if (parsed.password[0] == '\0') {
        strncpy(parsed.password, s_cfg.password, sizeof(parsed.password) - 1);
    }

    esp_err_t err = wifi_cfg_save(&parsed);
    if (err != ESP_OK) {
        int n = provisioning_render_form(resp_buf, 4096, &parsed,
                                          "Failed to save. Try again.");
        if (n < 0) { free(resp_buf); httpd_resp_send_500(req); return ESP_FAIL; }
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, resp_buf, n);
        free(resp_buf);
        return ESP_OK;
    }

    int n = provisioning_render_saved(resp_buf, 4096);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp_buf, n > 0 ? n : 0);
    free(resp_buf);

    vTaskDelay(pdMS_TO_TICKS(500));  // let the response flush before rebooting
    esp_restart();
    return ESP_OK;  // unreachable
}

void provisioning_start(const wifi_cfg_t *current) {
    s_cfg = *current;

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_dhcps_stop(ap_netif);

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 9, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 9, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    char ssid[32];
    provisioning_build_ssid(mac, ssid, sizeof ssid);

    wifi_config_t ap_config = { 0 };
    strncpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "provisioning AP '%s' up at 192.168.9.1", ssid);

    char ssid_ip[64];
    snprintf(ssid_ip, sizeof ssid_ip, "%s 192.168.9.1", ssid);
    display_show("Setup WiFi", ssid_ip);
    voice_play(VOICE_SETUP);

    xTaskCreate(dns_task, "prov_dns", 4096, NULL, 5, NULL);

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 4;
    http_cfg.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &http_cfg));

    httpd_uri_t root_uri = { .uri = "/*", .method = HTTP_GET, .handler = root_get_handler };
    httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &save_uri));

    // save_post_handler() calls esp_restart() on success; block here so
    // app_main doesn't fall through to starting audio/ws_client without a
    // working WiFi connection.
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
