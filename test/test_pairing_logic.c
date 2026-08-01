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

// A token that does not fit token_cap must be REJECTED, never silently
// truncated: a truncated token looks like a successful pairing, gets written
// to NVS, and then fails every handshake with no clue why. Same failure mode
// as the pairing-code buffer guarded below, but on the secret rather than the
// code. This is the behaviour a generic "copy the string, capped" JSON helper
// would quietly drop, so it is pinned here.
static void test_token_too_long_for_buffer_is_rejected(void) {
    char tok[8];
    int r = aa_parse_pair_status(
        "{\"data\":{\"claimed\":true,\"token\":\"0123456789ABCDEF\"}}",
        tok, sizeof tok);
    CHECK(r == -1);
}

// A token whose closing quote never arrives (truncated response body) is
// malformed, not a 7-character token.
static void test_unterminated_token_is_rejected(void) {
    char tok[64];
    int r = aa_parse_pair_status("{\"data\":{\"claimed\":true,\"token\":\"TOK123",
                                  tok, sizeof tok);
    CHECK(r == -1);
}

// "claimed" present but not a JSON boolean means the response is not the shape
// we expect — report malformed rather than guessing "not claimed yet" and
// polling forever.
static void test_non_boolean_claimed_is_rejected(void) {
    char tok[64];
    CHECK(aa_parse_pair_status("{\"data\":{\"claimed\":\"yes\"}}", tok, sizeof tok) == -1);
}

// Claimed, but no token field at all: also malformed.
static void test_claimed_without_token_is_rejected(void) {
    char tok[64];
    CHECK(aa_parse_pair_status("{\"data\":{\"claimed\":true}}", tok, sizeof tok) == -1);
}

// A response with no "data" envelope is not a pair/status body.
static void test_missing_data_envelope_is_rejected(void) {
    char tok[64];
    CHECK(aa_parse_pair_status("{\"claimed\":true,\"token\":\"T\"}", tok, sizeof tok) == -1);
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
    test_token_too_long_for_buffer_is_rejected();
    test_unterminated_token_is_rejected();
    test_non_boolean_claimed_is_rejected();
    test_claimed_without_token_is_rejected();
    test_missing_data_envelope_is_rejected();
    test_code_buffer_fits_server_code();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("OK\n");
    return 0;
}
