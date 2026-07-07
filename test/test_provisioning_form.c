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
    char buf[2048];
    int n = provisioning_render_form(buf, sizeof buf, &cfg, NULL);
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
    char buf[2048];
    int n = provisioning_render_form(buf, sizeof buf, &cfg, NULL);
    CHECK(n > 0);
    CHECK(strstr(buf, "<script>") == NULL);
    CHECK(strstr(buf, "&lt;script&gt;") != NULL);
}

static void test_render_form_shows_error(void) {
    wifi_cfg_t cfg = {0};
    strcpy(cfg.ssid, "MyHome");
    strcpy(cfg.server_host, "192.168.1.50");
    cfg.server_port = 8000;
    char buf[2048];
    int n = provisioning_render_form(buf, sizeof buf, &cfg, "bad input");
    CHECK(n > 0);
    CHECK(strstr(buf, "bad input") != NULL);
}

static void test_render_form_too_small(void) {
    wifi_cfg_t cfg = {0};
    strcpy(cfg.ssid, "MyHome");
    strcpy(cfg.server_host, "192.168.1.50");
    cfg.server_port = 8000;
    char buf[10];
    CHECK(provisioning_render_form(buf, sizeof buf, &cfg, NULL) == -1);
}

static void test_render_saved(void) {
    char buf[256];
    int n = provisioning_render_saved(buf, sizeof buf);
    CHECK(n > 0);
    CHECK(strstr(buf, "Restarting") != NULL);
}

static void test_render_saved_too_small(void) {
    char buf[4];
    CHECK(provisioning_render_saved(buf, sizeof buf) == -1);
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
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
