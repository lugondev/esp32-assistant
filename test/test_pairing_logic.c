#include "pairing.h"
#include <string.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_format_serial(void) {
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0x00, 0x1F, 0xE9};
    char out[13];
    aa_format_serial(mac, out);
    CHECK(strcmp(out, "aabbcc001fe9") == 0);
}

static void test_classify(void) {
    CHECK(aa_classify_disconnect(403, NULL) == AA_DISCONNECT_REPAIR);
    CHECK(aa_classify_disconnect(401, NULL) == AA_DISCONNECT_REPAIR);
    CHECK(aa_classify_disconnect(0, "account_disabled") == AA_DISCONNECT_REPAIR);
    CHECK(aa_classify_disconnect(0, "idle_timeout") == AA_DISCONNECT_RECONNECT);
    CHECK(aa_classify_disconnect(0, NULL) == AA_DISCONNECT_RECONNECT);
    CHECK(aa_classify_disconnect(500, NULL) == AA_DISCONNECT_RECONNECT);
}

static void test_parse_status(void) {
    char tok[64];
    CHECK(aa_parse_pair_status("{\"success\":true,\"data\":{\"claimed\":false}}", tok, sizeof tok) == 0);
    int r = aa_parse_pair_status(
        "{\"success\":true,\"data\":{\"claimed\":true,\"device_id\":\"d\",\"token\":\"TOK123\"}}",
        tok, sizeof tok);
    CHECK(r == 1);
    CHECK(strcmp(tok, "TOK123") == 0);
    CHECK(aa_parse_pair_status("not json", tok, sizeof tok) == -1);
    // Test pretty-printed JSON (whitespace after colons)
    int r2 = aa_parse_pair_status(
        "{\"success\":true,\"data\":{\"claimed\": true,\"device_id\":\"d\",\"token\": \"TOK123\"}}",
        tok, sizeof tok);
    CHECK(r2 == 1);
    CHECK(strcmp(tok, "TOK123") == 0);
}

// Regression guard for the 6->8 digit code widening on the server
// (api_gateway app/services/auth/pairing.py, `_CODE_DIGITS`). The buffer
// aa_run_pairing() parses the code into used to be a bare `char code[8]`,
// which cannot hold 8 digits + NUL: extract_str() stops at cap-1 chars, sees
// a digit where the closing quote should be, and returns -1 -- so pair/init
// "parse failed" forever and the device never displays a code at all. Keep
// this headroom check ahead of the server's digit count.
#define AA_SERVER_CODE_DIGITS 8
static void test_code_buffer_fits_server_code(void) {
    CHECK(AA_PAIR_CODE_MAX >= AA_SERVER_CODE_DIGITS + 1);
    // The buffer must also survive a plausible future widening without
    // another silent field-failure like this one.
    CHECK(AA_PAIR_CODE_MAX >= 12);
}

int main(void) {
    test_format_serial();
    test_classify();
    test_parse_status();
    test_code_buffer_fits_server_code();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("OK\n");
    return 0;
}
