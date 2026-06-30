#include "ws_protocol.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_parse_session_started(void) {
    wsp_event_t e;
    int rc = wsp_parse_event(
        "{\"event\":\"session_started\",\"output_sample_rate\":24000}", &e);
    CHECK(rc == 0);
    CHECK(e.type == WSP_EV_SESSION_STARTED);
    CHECK(e.sample_rate == 24000);
}

static void test_parse_audio_start(void) {
    wsp_event_t e;
    int rc = wsp_parse_event(
        "{\"event\":\"audio_start\",\"chunk_index\":2,\"codec\":\"opus\","
        "\"sample_rate\":24000,\"frames\":5}", &e);
    CHECK(rc == 0);
    CHECK(e.type == WSP_EV_AUDIO_START);
    CHECK(e.chunk_index == 2);
    CHECK(e.frames == 5);
    CHECK(e.sample_rate == 24000);
}

static void test_parse_user_transcript(void) {
    wsp_event_t e;
    int rc = wsp_parse_event(
        "{\"event\":\"user_transcript\",\"text\":\"xin chao\"}", &e);
    CHECK(rc == 0);
    CHECK(e.type == WSP_EV_USER_TRANSCRIPT);
    CHECK(strcmp(e.text, "xin chao") == 0);
}

static void test_parse_simple_events(void) {
    wsp_event_t e;
    struct { const char *name; wsp_event_type_t t; } cases[] = {
        {"speech_start", WSP_EV_SPEECH_START},
        {"speech_end", WSP_EV_SPEECH_END},
        {"processing", WSP_EV_PROCESSING},
        {"audio_end", WSP_EV_AUDIO_END},
        {"turn_done", WSP_EV_TURN_DONE},
        {"aborted", WSP_EV_ABORTED},
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        char buf[64];
        snprintf(buf, sizeof buf, "{\"event\":\"%s\"}", cases[i].name);
        CHECK(wsp_parse_event(buf, &e) == 0);
        CHECK(e.type == cases[i].t);
    }
}

static void test_parse_error_and_unknown(void) {
    wsp_event_t e;
    CHECK(wsp_parse_event("{\"event\":\"error\",\"message\":\"boom\"}", &e) == 0);
    CHECK(e.type == WSP_EV_ERROR);
    CHECK(strcmp(e.text, "boom") == 0);
    CHECK(wsp_parse_event("{\"event\":\"made_up\"}", &e) == 0);
    CHECK(e.type == WSP_EV_UNKNOWN);
    CHECK(wsp_parse_event("not json", &e) == -1);
}

int main(void) {
    test_parse_session_started();
    test_parse_audio_start();
    test_parse_user_transcript();
    test_parse_simple_events();
    test_parse_error_and_unknown();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
