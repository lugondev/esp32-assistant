#pragma once
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    WSP_EV_UNKNOWN = 0,
    WSP_EV_SESSION_STARTED,
    WSP_EV_SPEECH_START,
    WSP_EV_SPEECH_END,
    WSP_EV_PROCESSING,
    WSP_EV_USER_TRANSCRIPT,
    WSP_EV_RESPONSE_TEXT,
    WSP_EV_AUDIO_START,
    WSP_EV_AUDIO_END,
    WSP_EV_TURN_DONE,
    WSP_EV_ABORTED,
    WSP_EV_ERROR,
} wsp_event_type_t;

typedef struct {
    wsp_event_type_t type;
    int chunk_index;
    int frames;
    int sample_rate;
    char text[256];
} wsp_event_t;

// Parse one server JSON text frame. Returns 0 on success (including unknown
// event names -> WSP_EV_UNKNOWN), -1 if json is not valid JSON.
int wsp_parse_event(const char *json, wsp_event_t *out);

// Build a control message: {"type":"<type>"}. Returns length or -1 if too small.
int wsp_build_control(char *buf, size_t buflen, const char *type);

// Build a text message: {"type":"text","text":"<text>"} with JSON escaping.
// Returns length or -1 if too small.
int wsp_build_text(char *buf, size_t buflen, const char *text);
