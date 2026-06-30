#include "ws_protocol.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

// Locate the value for top-level "key"; returns pointer to first value char or NULL.
static const char *find_value(const char *json, const char *key) {
    char pat[64];
    int n = snprintf(pat, sizeof pat, "\"%s\"", key);
    if (n < 0 || (size_t)n >= sizeof pat) return NULL;
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p = skip_ws(p + n);
    if (*p != ':') return NULL;
    return skip_ws(p + 1);
}

// Copy string value for key into out (unescaping common escapes). out is "" if absent.
static void get_string(const char *json, const char *key, char *out, size_t cap) {
    if (cap == 0) return;
    out[0] = '\0';
    const char *p = find_value(json, key);
    if (!p || *p != '"') return;
    p++;
    size_t o = 0;
    while (*p && *p != '"') {
        char c = *p;
        if (c == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'n': c = '\n'; break; case 't': c = '\t'; break;
                case 'r': c = '\r'; break; case 'b': c = '\b'; break;
                case 'f': c = '\f'; break; default: c = *p; break;
            }
        }
        if (o < cap - 1) out[o++] = c;
        p++;
    }
    out[o] = '\0';
}

// Read integer value for key; returns 0 if absent/non-numeric.
static int get_int(const char *json, const char *key) {
    const char *p = find_value(json, key);
    if (!p) return 0;
    char *end;
    long v = strtol(p, &end, 10);
    return end == p ? 0 : (int)v;
}

int wsp_parse_event(const char *json, wsp_event_t *out) {
    memset(out, 0, sizeof(*out));
    if (*skip_ws(json) != '{') return -1;

    char name[64];
    get_string(json, "event", name, sizeof name);

    if (!strcmp(name, "session_started")) {
        out->type = WSP_EV_SESSION_STARTED;
        out->sample_rate = get_int(json, "output_sample_rate");
    } else if (!strcmp(name, "speech_start")) {
        out->type = WSP_EV_SPEECH_START;
    } else if (!strcmp(name, "speech_end")) {
        out->type = WSP_EV_SPEECH_END;
    } else if (!strcmp(name, "processing")) {
        out->type = WSP_EV_PROCESSING;
    } else if (!strcmp(name, "user_transcript")) {
        out->type = WSP_EV_USER_TRANSCRIPT;
        get_string(json, "text", out->text, sizeof out->text);
    } else if (!strcmp(name, "response_text")) {
        out->type = WSP_EV_RESPONSE_TEXT;
        out->chunk_index = get_int(json, "chunk_index");
        get_string(json, "text", out->text, sizeof out->text);
    } else if (!strcmp(name, "audio_start")) {
        out->type = WSP_EV_AUDIO_START;
        out->chunk_index = get_int(json, "chunk_index");
        out->frames = get_int(json, "frames");
        out->sample_rate = get_int(json, "sample_rate");
    } else if (!strcmp(name, "audio_end")) {
        out->type = WSP_EV_AUDIO_END;
        out->chunk_index = get_int(json, "chunk_index");
    } else if (!strcmp(name, "turn_done")) {
        out->type = WSP_EV_TURN_DONE;
    } else if (!strcmp(name, "aborted")) {
        out->type = WSP_EV_ABORTED;
        get_string(json, "reason", out->text, sizeof out->text);
    } else if (!strcmp(name, "error")) {
        out->type = WSP_EV_ERROR;
        get_string(json, "message", out->text, sizeof out->text);
    } else {
        out->type = WSP_EV_UNKNOWN;
    }
    return 0;
}
