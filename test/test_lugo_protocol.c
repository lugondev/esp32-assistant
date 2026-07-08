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
    int n = lugo_build_wakeup(buf, sizeof buf, "esp32-assistant", 16000, 16000, 60);
    CHECK(n > 0);
    CHECK(strstr(buf, "\"type\":\"wakeup\"") != NULL);
    CHECK(strstr(buf, "\"profile\":\"esp32-assistant\"") != NULL);
    CHECK(strstr(buf, "\"sample_rate\":16000") != NULL);
    CHECK(strstr(buf, "\"output_sample_rate\":16000") != NULL);
    CHECK(lugo_build_abort(buf, sizeof buf, "user") > 0);
    CHECK(strstr(buf, "\"type\":\"abort\"") != NULL);
    CHECK(strstr(buf, "\"reason\":\"user\"") != NULL);
    CHECK(lugo_build_text(buf, sizeof buf, "hi \"there\"") > 0);
    CHECK(strstr(buf, "\\\"there\\\"") != NULL);   // quotes escaped
    CHECK(lugo_build_abort(buf, 4, "user") == -1); // overflow
}

int main(void) {
    test_frame_roundtrip();
    test_frame_bad();
    test_parse_welcome();
    test_parse_tts_states();
    test_parse_stt_goodbye_error();
    test_parse_not_object();
    test_build_wakeup_and_controls();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("all lugo_protocol tests passed\n");
    return 0;
}
