#include "lugo_protocol.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_frame_roundtrip(void) {
    uint8_t payload[3] = {0xAA, 0xBB, 0xCC};
    uint8_t buf[16];
    int n = lugo_frame_encode(LUGO_FRAME_OPUS, payload, 3, buf, sizeof buf);
    CHECK(n == 7);
    CHECK(buf[0] == LUGO_FRAME_OPUS);
    CHECK(buf[1] == 0);
    CHECK(buf[2] == 0 && buf[3] == 3);   // big-endian size
    uint8_t type; const uint8_t *p; int plen;
    CHECK(lugo_frame_decode(buf, n, &type, &p, &plen) == 0);
    CHECK(type == LUGO_FRAME_OPUS);
    CHECK(plen == 3);
    CHECK(memcmp(p, payload, 3) == 0);
}

static void test_frame_bad(void) {
    uint8_t type; const uint8_t *p; int plen;
    uint8_t two[2] = {0, 0};
    CHECK(lugo_frame_decode(two, 2, &type, &p, &plen) == -1);   // shorter than header
    uint8_t bad[6] = {0, 0, 0, 5, 1, 2};                        // says 5, has 2
    CHECK(lugo_frame_decode(bad, 6, &type, &p, &plen) == -1);
    uint8_t small[2];
    CHECK(lugo_frame_encode(LUGO_FRAME_OPUS, (const uint8_t *)"xy", 2, small, 2) == -1);  // no room
}

static void test_parse_welcome(void) {
    lugo_event_t e;
    int rc = lugo_parse_event(
        "{\"type\":\"welcome\",\"session_id\":\"x\","
        "\"audio_params\":{\"sample_rate\":16000},\"idle_timeout_s\":30}", &e);
    CHECK(rc == 0);
    CHECK(e.type == LUGO_EV_WELCOME);
    CHECK(e.sample_rate == 16000);
    CHECK(e.idle_timeout_s == 30);
}

static void test_parse_tts_states(void) {
    lugo_event_t e;
    CHECK(lugo_parse_event("{\"type\":\"tts\",\"state\":\"start\"}", &e) == 0);
    CHECK(e.type == LUGO_EV_TTS_START);
    CHECK(lugo_parse_event("{\"type\":\"tts\",\"state\":\"stop\"}", &e) == 0);
    CHECK(e.type == LUGO_EV_TTS_STOP);
    CHECK(lugo_parse_event("{\"type\":\"tts\",\"state\":\"sentence_start\",\"text\":\"chao\"}", &e) == 0);
    CHECK(e.type == LUGO_EV_TTS_SENTENCE);
    CHECK(strcmp(e.text, "chao") == 0);
}

static void test_parse_stt_goodbye_error(void) {
    lugo_event_t e;
    CHECK(lugo_parse_event("{\"type\":\"stt\",\"text\":\"xin chao\",\"final\":true}", &e) == 0);
    CHECK(e.type == LUGO_EV_STT);
    CHECK(strcmp(e.text, "xin chao") == 0);
    CHECK(lugo_parse_event("{\"type\":\"goodbye\",\"reason\":\"idle_timeout\"}", &e) == 0);
    CHECK(e.type == LUGO_EV_GOODBYE);
    CHECK(strcmp(e.text, "idle_timeout") == 0);
    CHECK(lugo_parse_event("{\"type\":\"error\",\"message\":\"boom\"}", &e) == 0);
    CHECK(e.type == LUGO_EV_ERROR);
    CHECK(strcmp(e.text, "boom") == 0);
}

static void test_parse_not_object(void) {
    lugo_event_t e;
    CHECK(lugo_parse_event("42", &e) == -1);
    CHECK(lugo_parse_event("[1,2]", &e) == -1);
}

static void test_build_wakeup_and_controls(void) {
    char buf[256];
    int n = lugo_build_wakeup(buf, sizeof buf, 16000, 16000, 60);
    CHECK(n > 0);
    CHECK(strstr(buf, "\"type\":\"wakeup\"") != NULL);
    // No `profile` key at all: the gateway owns that choice. A paired device's
    // server-side binding outranks anything the wakeup declares (see
    // resolve_bound_profile in the gateway's lugo route), so sending one here
    // was at best ignored and at worst a stale name the device could not fix.
    CHECK(strstr(buf, "profile") == NULL);
    CHECK(strstr(buf, "\"sample_rate\":16000") != NULL);
    CHECK(strstr(buf, "\"output_sample_rate\":16000") != NULL);
    CHECK(lugo_build_abort(buf, sizeof buf, "user") > 0);
    CHECK(strstr(buf, "\"type\":\"abort\"") != NULL);
    CHECK(strstr(buf, "\"reason\":\"user\"") != NULL);
    CHECK(lugo_build_text(buf, sizeof buf, "hi \"there\"") > 0);
    CHECK(strstr(buf, "\\\"there\\\"") != NULL);   // quotes escaped
    CHECK(lugo_build_abort(buf, 4, "user") == -1); // overflow
}

static void test_build_new_session(void) {
    char buf[64];
    int n = lugo_build_new_session(buf, sizeof buf);
    CHECK(n > 0);
    CHECK(strcmp(buf, "{\"type\":\"new_session\"}") == 0);
    // Deliberately carries no session_id: an id here would read as a RESUME
    // request on the gateway, the exact opposite of starting fresh.
    CHECK(strstr(buf, "session_id") == NULL);
    CHECK(lugo_build_new_session(buf, 8) == -1);  // overflow fails closed
}

static void test_wakeup_advertises_mcp_feature(void) {
    char buf[256];
    int n = lugo_build_wakeup(buf, sizeof buf, 16000, 24000, 60);
    CHECK(n > 0);
    CHECK(strstr(buf, "\"features\"") != NULL);
    CHECK(strstr(buf, "\"mcp\":true") != NULL);
}

static void test_mcp_payload_pointer(void) {
    const char *json =
        "{\"type\":\"mcp\",\"payload\":{\"jsonrpc\":\"2.0\",\"id\":3,"
        "\"method\":\"tools/call\",\"params\":{\"name\":\"self.audio.set_volume\","
        "\"arguments\":{\"volume\":70}}}}";
    lugo_event_t e;
    CHECK(lugo_parse_event(json, &e) == 0);
    CHECK(e.type == LUGO_EV_MCP);
    CHECK(e.mcp_payload != NULL);
    CHECK(e.mcp_payload[0] == '{');
    CHECK(lugo_json_get_int(e.mcp_payload, "id") == 3);
    char method[32];
    lugo_json_get_string(e.mcp_payload, "method", method, sizeof method);
    CHECK(strcmp(method, "tools/call") == 0);
}

static void test_json_get_bool(void) {
    CHECK(lugo_json_get_bool("{\"confirm\":true}", "confirm", 0) == 1);
    CHECK(lugo_json_get_bool("{\"confirm\":false}", "confirm", 1) == 0);
    CHECK(lugo_json_get_bool("{\"other\":1}", "confirm", 1) == 1);   // default when absent
    CHECK(lugo_json_get_bool("{\"other\":1}", "confirm", 0) == 0);
}

static void test_json_find_returns_object_pointer(void) {
    const char *p = lugo_json_find("{\"a\":1,\"payload\":{\"x\":5}}", "payload");
    CHECK(p != NULL);
    CHECK(p[0] == '{');
    CHECK(lugo_json_get_int(p, "x") == 5);
}

// The strict variant exists for values where a truncated copy is worse than no
// copy at all — a device token, a pairing code. The lenient
// lugo_json_get_string() keeps its truncating behaviour for display strings.
static void test_json_get_string_strict_accepts_a_fitting_value(void) {
    char out[8];
    CHECK(lugo_json_get_string_strict("{\"token\":\"ABC\"}", "token", out, sizeof out) == 0);
    CHECK(strcmp(out, "ABC") == 0);
}

static void test_json_get_string_strict_accepts_an_exactly_fitting_value(void) {
    char out[4];   // 3 chars + NUL
    CHECK(lugo_json_get_string_strict("{\"token\":\"ABC\"}", "token", out, sizeof out) == 0);
    CHECK(strcmp(out, "ABC") == 0);
}

static void test_json_get_string_strict_rejects_an_oversized_value(void) {
    char out[4];
    CHECK(lugo_json_get_string_strict("{\"token\":\"ABCD\"}", "token", out, sizeof out) == -1);
}

static void test_json_get_string_strict_rejects_an_unterminated_value(void) {
    char out[32];
    CHECK(lugo_json_get_string_strict("{\"token\":\"ABC", "token", out, sizeof out) == -1);
}

static void test_json_get_string_strict_rejects_absent_or_non_string(void) {
    char out[32];
    CHECK(lugo_json_get_string_strict("{\"other\":1}", "token", out, sizeof out) == -1);
    CHECK(lugo_json_get_string_strict("{\"token\":42}", "token", out, sizeof out) == -1);
}

// Escapes are unescaped like the lenient variant, and the fit is judged on the
// DECODED length — "\\n" is two source characters but one output character.
static void test_json_get_string_strict_measures_the_decoded_length(void) {
    char out[3];   // 2 chars + NUL
    CHECK(lugo_json_get_string_strict("{\"t\":\"a\\nb\"}", "t", out, sizeof out) == -1);
    char out2[4];
    CHECK(lugo_json_get_string_strict("{\"t\":\"a\\nb\"}", "t", out2, sizeof out2) == 0);
    CHECK(strcmp(out2, "a\nb") == 0);
}

int main(void) {
    test_frame_roundtrip();
    test_frame_bad();
    test_parse_welcome();
    test_parse_tts_states();
    test_parse_stt_goodbye_error();
    test_parse_not_object();
    test_build_wakeup_and_controls();
    test_build_new_session();
    test_wakeup_advertises_mcp_feature();
    test_mcp_payload_pointer();
    test_json_get_bool();
    test_json_find_returns_object_pointer();
    test_json_get_string_strict_accepts_a_fitting_value();
    test_json_get_string_strict_accepts_an_exactly_fitting_value();
    test_json_get_string_strict_rejects_an_oversized_value();
    test_json_get_string_strict_rejects_an_unterminated_value();
    test_json_get_string_strict_rejects_absent_or_non_string();
    test_json_get_string_strict_measures_the_decoded_length();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("all lugo_protocol tests passed\n");
    return 0;
}
