#include "provisioning_form.h"
#include <string.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_parse_basic(void) {
    wifi_cfg_t cfg = {0};
    const char *body = "ssid=MyHome&password=secret123&host=192.168.1.50&port=8000";
    int rc = provisioning_parse_form(body, strlen(body), &cfg);
    CHECK(rc == 0);
    CHECK(strcmp(cfg.ssid, "MyHome") == 0);
    CHECK(strcmp(cfg.password, "secret123") == 0);
    CHECK(strcmp(cfg.server_host, "192.168.1.50") == 0);
    CHECK(cfg.server_port == 8000);
}

static void test_parse_url_encoded(void) {
    wifi_cfg_t cfg = {0};
    const char *body = "ssid=My+Home&password=p%40ss%21&host=gw.local&port=443";
    int rc = provisioning_parse_form(body, strlen(body), &cfg);
    CHECK(rc == 0);
    CHECK(strcmp(cfg.ssid, "My Home") == 0);
    CHECK(strcmp(cfg.password, "p@ss!") == 0);
    CHECK(strcmp(cfg.server_host, "gw.local") == 0);
    CHECK(cfg.server_port == 443);
}

static void test_parse_missing_ssid(void) {
    wifi_cfg_t cfg = {0};
    const char *body = "password=secret&host=192.168.1.50&port=8000";
    CHECK(provisioning_parse_form(body, strlen(body), &cfg) == -1);
}

static void test_parse_empty_ssid(void) {
    wifi_cfg_t cfg = {0};
    const char *body = "ssid=&host=192.168.1.50&port=8000";
    CHECK(provisioning_parse_form(body, strlen(body), &cfg) == -1);
}

static void test_parse_no_password_field_defaults_empty(void) {
    wifi_cfg_t cfg = {0};
    strcpy(cfg.password, "leftover");
    const char *body = "ssid=MyHome&host=192.168.1.50&port=8000";
    CHECK(provisioning_parse_form(body, strlen(body), &cfg) == 0);
    CHECK(cfg.password[0] == '\0');
}

static void test_parse_bad_port_non_numeric(void) {
    wifi_cfg_t cfg = {0};
    const char *body = "ssid=MyHome&host=192.168.1.50&port=notanumber";
    CHECK(provisioning_parse_form(body, strlen(body), &cfg) == -1);
}

static void test_parse_port_out_of_range(void) {
    wifi_cfg_t cfg = {0};
    const char *body = "ssid=MyHome&host=192.168.1.50&port=70000";
    CHECK(provisioning_parse_form(body, strlen(body), &cfg) == -1);
}

static void test_parse_missing_host(void) {
    wifi_cfg_t cfg = {0};
    const char *body = "ssid=MyHome&port=8000";
    CHECK(provisioning_parse_form(body, strlen(body), &cfg) == -1);
}

static void test_render_form_contains_values_not_password(void) {
    wifi_cfg_t cfg = {0};
    strcpy(cfg.ssid, "MyHome");
    strcpy(cfg.password, "supersecret");
    strcpy(cfg.server_host, "192.168.1.50");
    cfg.server_port = 8000;
    char buf[PROV_PAGE_BUF];
    int n = provisioning_render_form(buf, sizeof buf, &cfg, NULL, NULL, 0);
    CHECK(n > 0);
    CHECK(strstr(buf, "MyHome") != NULL);
    CHECK(strstr(buf, "192.168.1.50") != NULL);
    CHECK(strstr(buf, "8000") != NULL);
    CHECK(strstr(buf, "supersecret") == NULL);
}

static void test_render_form_escapes_html(void) {
    wifi_cfg_t cfg = {0};
    strcpy(cfg.ssid, "My\"Net<script>");
    strcpy(cfg.server_host, "host");
    cfg.server_port = 80;
    char buf[PROV_PAGE_BUF];
    int n = provisioning_render_form(buf, sizeof buf, &cfg, NULL, NULL, 0);
    CHECK(n > 0);
    // The page itself now carries a legitimate <script> block (the network
    // picker's click handler), so "no <script> anywhere" is no longer the
    // right assertion — what matters is that the SSID's markup arrived inert
    // and never appears raw.
    CHECK(strstr(buf, "My\"Net<script>") == NULL);
    CHECK(strstr(buf, "My&quot;Net&lt;script&gt;") != NULL);
}

static void test_render_form_shows_error(void) {
    wifi_cfg_t cfg = {0};
    strcpy(cfg.ssid, "MyHome");
    strcpy(cfg.server_host, "192.168.1.50");
    cfg.server_port = 8000;
    char buf[PROV_PAGE_BUF];
    int n = provisioning_render_form(buf, sizeof buf, &cfg, "bad input", NULL, 0);
    CHECK(n > 0);
    CHECK(strstr(buf, "bad input") != NULL);
}

static void test_render_form_too_small(void) {
    wifi_cfg_t cfg = {0};
    strcpy(cfg.ssid, "MyHome");
    strcpy(cfg.server_host, "192.168.1.50");
    cfg.server_port = 8000;
    char buf[10];
    CHECK(provisioning_render_form(buf, sizeof buf, &cfg, NULL, NULL, 0) == -1);
}

static void test_render_saved(void) {
    char buf[PROV_PAGE_BUF];
    int n = provisioning_render_saved(buf, sizeof buf);
    CHECK(n > 0);
    CHECK(strstr(buf, "Restarting") != NULL);
}

static void test_render_saved_too_small(void) {
    char buf[4];
    CHECK(provisioning_render_saved(buf, sizeof buf) == -1);
}

// --- scan list ------------------------------------------------------------

static prov_network_t mknet(const char *ssid, int rssi, bool secure) {
    prov_network_t n = {0};
    snprintf(n.ssid, sizeof n.ssid, "%s", ssid);
    n.rssi = rssi;
    n.secure = secure;
    return n;
}

static void test_sort_orders_strongest_first(void) {
    prov_network_t nets[3] = { mknet("far", -80, true), mknet("near", -35, true),
                               mknet("mid", -60, false) };
    int n = provisioning_sort_networks(nets, 3);
    CHECK(n == 3);
    CHECK(strcmp(nets[0].ssid, "near") == 0);
    CHECK(strcmp(nets[1].ssid, "mid") == 0);
    CHECK(strcmp(nets[2].ssid, "far") == 0);
}

static void test_sort_dedups_keeping_strongest(void) {
    // The 2.4/5 GHz pair of one router, plus a mesh node repeating it.
    prov_network_t nets[4] = { mknet("Home", -70, true), mknet("Other", -75, true),
                               mknet("Home", -40, true), mknet("Home", -66, true) };
    int n = provisioning_sort_networks(nets, 4);
    CHECK(n == 2);
    CHECK(strcmp(nets[0].ssid, "Home") == 0);
    CHECK(nets[0].rssi == -40);   // strongest sighting survives
    CHECK(strcmp(nets[1].ssid, "Other") == 0);
}

static void test_sort_drops_hidden_ssids(void) {
    prov_network_t nets[3] = { mknet("", -40, true), mknet("Real", -70, true),
                               mknet("", -50, false) };
    int n = provisioning_sort_networks(nets, 3);
    CHECK(n == 1);
    CHECK(strcmp(nets[0].ssid, "Real") == 0);
}

static void test_sort_caps_at_max(void) {
    prov_network_t nets[PROV_MAX_NETWORKS + 7];
    for (int i = 0; i < PROV_MAX_NETWORKS + 7; i++) {
        char name[16];
        snprintf(name, sizeof name, "net%d", i);
        nets[i] = mknet(name, -40 - i, true);
    }
    CHECK(provisioning_sort_networks(nets, PROV_MAX_NETWORKS + 7) == PROV_MAX_NETWORKS);
}

static void test_sort_handles_empty(void) {
    CHECK(provisioning_sort_networks(NULL, 0) == 0);
    prov_network_t one = mknet("x", -50, true);
    CHECK(provisioning_sort_networks(&one, 0) == 0);
}

static void test_render_lists_networks(void) {
    wifi_cfg_t cfg = {0};
    strcpy(cfg.ssid, "Home");
    strcpy(cfg.server_host, "gw.local");
    cfg.server_port = 8000;
    prov_network_t nets[2] = { mknet("Home", -40, true), mknet("Cafe", -80, false) };
    char buf[PROV_PAGE_BUF];
    int n = provisioning_render_form(buf, sizeof buf, &cfg, NULL, nets, 2);
    CHECK(n > 0);
    CHECK(strstr(buf, "data-s=\"Home\"") != NULL);
    CHECK(strstr(buf, "data-s=\"Cafe\"") != NULL);
    CHECK(strstr(buf, "bars b4") != NULL);   // -40 dBm
    CHECK(strstr(buf, "bars b1") != NULL);   // -80 dBm
    // Padlock only on the secured one: exactly one <use> reference.
    CHECK(strstr(buf, "<use href=#lk />") != NULL);
    const char *first = strstr(buf, "<use href=#lk />");
    CHECK(strstr(first + 1, "<use href=#lk />") == NULL);
    // The configured network comes back pre-selected.
    CHECK(strstr(buf, "aria-pressed=true") != NULL);
}

static void test_render_escapes_scanned_ssid(void) {
    // A hostile AP name must not be able to break out of the data-s attribute
    // or the row label — the picker copies it via .dataset, but the attribute
    // itself is still written into markup here.
    wifi_cfg_t cfg = {0};
    strcpy(cfg.server_host, "gw.local");
    cfg.server_port = 8000;
    prov_network_t nets[1] = { mknet("\"><img src=x onerror=alert(1)>", -50, true) };
    char buf[PROV_PAGE_BUF];
    int n = provisioning_render_form(buf, sizeof buf, &cfg, NULL, nets, 1);
    CHECK(n > 0);
    CHECK(strstr(buf, "<img") == NULL);
    CHECK(strstr(buf, "onerror") != NULL);        // present, but as inert text
    CHECK(strstr(buf, "&quot;&gt;&lt;img") != NULL);
}

static void test_render_no_networks_shows_manual_hint(void) {
    wifi_cfg_t cfg = {0};
    strcpy(cfg.server_host, "gw.local");
    cfg.server_port = 8000;
    char buf[PROV_PAGE_BUF];
    int n = provisioning_render_form(buf, sizeof buf, &cfg, NULL, NULL, 0);
    CHECK(n > 0);
    CHECK(strstr(buf, "No networks found") != NULL);
    CHECK(strstr(buf, "class=nets") == NULL);
    // The SSID field is still there, so a scan that found nothing is
    // recoverable by typing the name.
    CHECK(strstr(buf, "name=ssid") != NULL);
}

// A full page of max-length SSIDs is the worst case PROV_PAGE_BUF has to
// absorb; if this ever stops fitting the portal silently 500s on the device.
static void test_render_full_list_fits_page_buffer(void) {
    wifi_cfg_t cfg = {0};
    strcpy(cfg.server_host, "gateway.example.internal");
    cfg.server_port = 65535;
    prov_network_t nets[PROV_MAX_NETWORKS];
    for (int i = 0; i < PROV_MAX_NETWORKS; i++) {
        char name[WIFI_CFG_SSID_MAX + 1];
        memset(name, 'W', WIFI_CFG_SSID_MAX);
        name[WIFI_CFG_SSID_MAX] = '\0';
        name[0] = (char)('a' + i);
        nets[i] = mknet(name, -40 - i, true);
    }
    char buf[PROV_PAGE_BUF];
    int n = provisioning_render_form(buf, sizeof buf, &cfg, "an error to render too",
                                      nets, PROV_MAX_NETWORKS);
    CHECK(n > 0);
    printf("--- portal page: %d B used of %d B (%d B free)\n",
           n, PROV_PAGE_BUF, PROV_PAGE_BUF - n);
}

int main(void) {
    test_parse_basic();
    test_parse_url_encoded();
    test_parse_missing_ssid();
    test_parse_empty_ssid();
    test_parse_no_password_field_defaults_empty();
    test_parse_bad_port_non_numeric();
    test_parse_port_out_of_range();
    test_parse_missing_host();
    test_render_form_contains_values_not_password();
    test_render_form_escapes_html();
    test_render_form_shows_error();
    test_render_form_too_small();
    test_render_saved();
    test_render_saved_too_small();
    test_sort_orders_strongest_first();
    test_sort_dedups_keeping_strongest();
    test_sort_drops_hidden_ssids();
    test_sort_caps_at_max();
    test_sort_handles_empty();
    test_render_lists_networks();
    test_render_escapes_scanned_ssid();
    test_render_no_networks_shows_manual_hint();
    test_render_full_list_fits_page_buffer();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
