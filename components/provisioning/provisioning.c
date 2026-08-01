#include "provisioning.h"
#include "provisioning_ssid.h"
#include "provisioning_form.h"
#include "wifi_sta.h"
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

// Networks shown in the portal's picker, captured once by scan_networks()
// before the radio switches to AP mode (see provisioning_start).
static prov_network_t s_nets[PROV_MAX_NETWORKS];
static int s_n_nets;

// Snapshot the surrounding APs while the radio is still in STA mode.
//
// Timing is the whole trick here: this MUST run before esp_wifi_set_mode(AP).
// A scan needs a station interface, and the portal deliberately runs AP-only
// (an APSTA portal let wifi_sta's reconnect handler keep sweeping channels,
// which starves the AP beacon and makes "Lugo-XXXX" unfindable — the reason
// the mode is pinned in the first place). Scanning first, then committing to
// AP-only, gets a real network list without reintroducing that.
//
// The consequence is that the list is a snapshot, not live: there is no
// rescan button, because serving one would need the STA interface back. A
// network that appears later is still reachable by typing its name into the
// SSID field, which the form keeps as a plain input for exactly this case.
static void scan_networks(void) {
    // Take wifi_sta out of the loop first. Its reconnect backoff is still
    // running whenever we got here from a failed connect (rather than from an
    // unconfigured first boot), and an esp_wifi_connect() landing mid-scan
    // contends for the same radio.
    wifi_sta_suspend();

    wifi_scan_config_t scan = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&scan, true);   // blocking
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi scan failed (%s) — portal will offer manual entry only",
                 esp_err_to_name(err));
        return;
    }

    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found == 0) { ESP_LOGW(TAG, "wifi scan found no networks"); return; }

    // Pull more records than we will show: the list is de-duplicated by SSID
    // afterwards, and a dual-band router or a mesh burns several records on one
    // network, so fetching only PROV_MAX_NETWORKS would truncate real networks
    // before dedup ever ran.
    uint16_t want = found > 40 ? 40 : found;
    wifi_ap_record_t *recs = calloc(want, sizeof(*recs));
    if (!recs) { esp_wifi_clear_ap_list(); return; }   // release the driver's copy
    esp_wifi_scan_get_ap_records(&want, recs);

    prov_network_t *all = calloc(want, sizeof(*all));
    if (!all) { free(recs); return; }
    for (uint16_t i = 0; i < want; i++) {
        snprintf(all[i].ssid, sizeof all[i].ssid, "%s", (const char *)recs[i].ssid);
        all[i].rssi = recs[i].rssi;
        all[i].secure = recs[i].authmode != WIFI_AUTH_OPEN;
    }
    int n = provisioning_sort_networks(all, (int)want);
    for (int i = 0; i < n; i++) s_nets[i] = all[i];
    s_n_nets = n;

    free(all);
    free(recs);
    ESP_LOGI(TAG, "wifi scan: %u records -> %d networks offered", (unsigned)found, s_n_nets);
}

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
    char *buf = malloc(PROV_PAGE_BUF);
    if (!buf) return ESP_ERR_NO_MEM;
    int n = provisioning_render_form(buf, PROV_PAGE_BUF, &s_cfg, NULL, s_nets, s_n_nets);
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

    char *resp_buf = malloc(PROV_PAGE_BUF);
    if (!resp_buf) return ESP_ERR_NO_MEM;

    if (rc != 0) {
        // Re-render with what the user actually typed, not with s_cfg. Handing
        // back the stored config threw away their entries on every validation
        // failure — pick the wrong port and you also lost the network name you
        // had just selected. provisioning_parse_form fills *out left to right
        // and stops at the first bad field, so whatever it did manage to parse
        // is good; anything it never reached stays empty and falls back to the
        // stored value.
        wifi_cfg_t shown = s_cfg;
        if (parsed.ssid[0])        snprintf(shown.ssid, sizeof shown.ssid, "%s", parsed.ssid);
        if (parsed.server_host[0]) snprintf(shown.server_host, sizeof shown.server_host, "%s", parsed.server_host);
        if (parsed.server_port > 0) shown.server_port = parsed.server_port;
        int n = provisioning_render_form(resp_buf, PROV_PAGE_BUF, &shown,
            "Invalid input: network name and gateway host are required, port must be 1-65535.",
            s_nets, s_n_nets);
        if (n < 0) { free(resp_buf); httpd_resp_send_500(req); return ESP_FAIL; }
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, resp_buf, n);
        free(resp_buf);
        return ESP_OK;
    }

    // The form never pre-fills the password field; if left blank, keep the
    // previously-saved one instead of wiping it.
    if (parsed.password[0] == '\0') {
        // snprintf (not strncpy) to always NUL-terminate and avoid the riscv
        // GCC -Werror=stringop-truncation the C3 toolchain raises. Behaviour-
        // preserving: copies up to sizeof-1 chars, always terminated.
        snprintf(parsed.password, sizeof(parsed.password), "%s", s_cfg.password);
    }

    esp_err_t err = wifi_cfg_save(&parsed);
    if (err != ESP_OK) {
        int n = provisioning_render_form(resp_buf, PROV_PAGE_BUF, &parsed,
                                          "Failed to save. Try again.", s_nets, s_n_nets);
        if (n < 0) { free(resp_buf); httpd_resp_send_500(req); return ESP_FAIL; }
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, resp_buf, n);
        free(resp_buf);
        return ESP_OK;
    }

    int n = provisioning_render_saved(resp_buf, PROV_PAGE_BUF);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp_buf, n > 0 ? n : 0);
    free(resp_buf);

    vTaskDelay(pdMS_TO_TICKS(500));  // let the response flush before rebooting
    esp_restart();
    return ESP_OK;  // unreachable
}

void provisioning_start(const wifi_cfg_t *current) {
    s_cfg = *current;

    // Before anything switches the radio: grab the network list while a station
    // interface still exists (see scan_networks). Also shows the user something
    // is happening — a scan takes a couple of seconds.
    display_show("Scanning WiFi", "Please wait...");
    scan_networks();

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
    snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "%s", ssid);
    ap_config.ap.ssid_len = strlen(ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    // AP-only, NOT APSTA: the portal takes the SSID from a manual form and
    // saves+reboots (no live STA scan needed), so keeping STA active only lets
    // wifi_sta's reconnect handler keep scanning all channels — on the single
    // radio that starves the AP beacon and makes "Lugo-XXXX" unfindable. Pure
    // AP parks the radio on the AP channel so the beacon is stable. Any stray
    // esp_wifi_connect() from the reconnect timer is a harmless no-op in AP mode.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
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
