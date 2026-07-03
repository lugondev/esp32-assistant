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

static void test_build_control(void) {
    char buf[64];
    int n = wsp_build_control(buf, sizeof buf, "flush");
    CHECK(n > 0);
    CHECK(strcmp(buf, "{\"type\":\"flush\"}") == 0);
}

static void test_build_text_escapes(void) {
    char buf[128];
    int n = wsp_build_text(buf, sizeof buf, "say \"hi\"");
    CHECK(n > 0);
    CHECK(strcmp(buf, "{\"type\":\"text\",\"text\":\"say \\\"hi\\\"\"}") == 0);
}

static void test_build_too_small(void) {
    char buf[4];
    CHECK(wsp_build_control(buf, sizeof buf, "flush") == -1);
}

static void test_build_text_control_char(void) {
    char buf[64];
    const char text[] = {'a', '\x01', 'b', '\0'};
    int n = wsp_build_text(buf, sizeof buf, text);
    CHECK(n > 0);
    CHECK(strcmp(buf, "{\"type\":\"text\",\"text\":\"a\\u0001b\"}") == 0);
}

static void test_build_text_too_small(void) {
    char buf[8];
    CHECK(wsp_build_text(buf, sizeof buf, "hello world") == -1);
}

static void test_build_uri(void) {
    wsp_config_t cfg = {
        .host = "192.168.1.50", .port = 8000, .secure = false,
        .stt_engine = "whisper_mlx", .tts_engine = "vieneu",
        .language = "vi", .sample_rate = 16000, .output_sample_rate = 24000,
    };
    char buf[512];
    int n = wsp_build_uri(buf, sizeof buf, &cfg);
    CHECK(n > 0);
    CHECK(strcmp(buf,
        "ws://192.168.1.50:8000/v1/conversation/stream"
        "?stt_engine=whisper_mlx&tts_engine=vieneu&language=vi"
        "&sample_rate=16000&audio_codec=opus&output=audio,text"
        "&audio_out=opus&output_sample_rate=24000") == 0);
}

static void test_build_uri_secure(void) {
    wsp_config_t cfg = { .host = "h", .port = 443, .secure = true,
        .stt_engine = "whisper_mlx", .tts_engine = "vieneu", .language = "vi",
        .sample_rate = 16000, .output_sample_rate = 24000 };
    char buf[512];
    CHECK(wsp_build_uri(buf, sizeof buf, &cfg) > 0);
    CHECK(strncmp(buf, "wss://h:443/", 12) == 0);
}

static void test_build_uri_with_profile(void) {
    wsp_config_t cfg = {
        .host = "192.168.1.50", .port = 8000, .secure = false,
        .stt_engine = "whisper_mlx", .tts_engine = "vieneu",
        .language = "vi", .sample_rate = 16000, .output_sample_rate = 24000,
        .profile = "kitchen",
    };
    char buf[512];
    int n = wsp_build_uri(buf, sizeof buf, &cfg);
    CHECK(n > 0);
    CHECK(strcmp(buf,
        "ws://192.168.1.50:8000/v1/conversation/stream"
        "?stt_engine=whisper_mlx&tts_engine=vieneu&language=vi"
        "&sample_rate=16000&audio_codec=opus&output=audio,text"
        "&audio_out=opus&output_sample_rate=24000&profile=kitchen") == 0);
}

static void test_build_uri_empty_profile_omitted(void) {
    wsp_config_t cfg = {
        .host = "192.168.1.50", .port = 8000, .secure = false,
        .stt_engine = "whisper_mlx", .tts_engine = "vieneu",
        .language = "vi", .sample_rate = 16000, .output_sample_rate = 24000,
        .profile = "",
    };
    char buf[512];
    CHECK(wsp_build_uri(buf, sizeof buf, &cfg) > 0);
    CHECK(strstr(buf, "profile=") == NULL);
}

static void test_build_uri_profile_too_small(void) {
    wsp_config_t cfg = {
        .host = "192.168.1.50", .port = 8000, .secure = false,
        .stt_engine = "whisper_mlx", .tts_engine = "vieneu",
        .language = "vi", .sample_rate = 16000, .output_sample_rate = 24000,
        .profile = "kitchen",
    };
    char buf[200];  // fits the 191-char base URI but not "&profile=kitchen" appended
    CHECK(wsp_build_uri(buf, sizeof buf, &cfg) == -1);
}

int main(void) {
    test_parse_session_started();
    test_parse_audio_start();
    test_parse_user_transcript();
    test_parse_simple_events();
    test_parse_error_and_unknown();
    test_build_control();
    test_build_text_escapes();
    test_build_too_small();
    test_build_text_control_char();
    test_build_text_too_small();
    test_build_uri();
    test_build_uri_secure();
    test_build_uri_with_profile();
    test_build_uri_empty_profile_omitted();
    test_build_uri_profile_too_small();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
