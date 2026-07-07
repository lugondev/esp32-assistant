#include "provisioning_ssid.h"
#include <string.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_basic(void) {
    uint8_t mac[6] = {0x28, 0x84, 0x85, 0x50, 0x48, 0xD0};
    char buf[32];
    int n = provisioning_build_ssid(mac, buf, sizeof buf);
    CHECK(n == 9);
    CHECK(strcmp(buf, "Lugo-48D0") == 0);
}

static void test_zero_mac(void) {
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    char buf[32];
    int n = provisioning_build_ssid(mac, buf, sizeof buf);
    CHECK(n == 9);
    CHECK(strcmp(buf, "Lugo-0000") == 0);
}

static void test_buf_exact_fit(void) {
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    char buf[10];  // "Lugo-0000" (9 chars) + NUL = 10
    CHECK(provisioning_build_ssid(mac, buf, sizeof buf) == 9);
}

static void test_buf_too_small(void) {
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    char buf[5];
    CHECK(provisioning_build_ssid(mac, buf, sizeof buf) == -1);
}

int main(void) {
    test_basic();
    test_zero_mac();
    test_buf_exact_fit();
    test_buf_too_small();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
