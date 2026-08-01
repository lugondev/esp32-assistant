#include "lugo_protocol.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

int lugo_frame_encode(uint8_t type, const uint8_t *payload, int len,
                      uint8_t *out, int out_cap) {
    if (len < 0 || len > 0xFFFF) return -1;
    if (out_cap < LUGO_FRAME_HEADER + len) return -1;
    out[0] = type;
    out[1] = 0;
    out[2] = (uint8_t)((len >> 8) & 0xFF);
    out[3] = (uint8_t)(len & 0xFF);
    if (len > 0) memcpy(out + LUGO_FRAME_HEADER, payload, (size_t)len);
    return LUGO_FRAME_HEADER + len;
}

int lugo_frame_decode(const uint8_t *data, int len, uint8_t *out_type,
                      const uint8_t **payload, int *payload_len) {
    if (len < LUGO_FRAME_HEADER) return -1;
    int size = (data[2] << 8) | data[3];
    if (size != len - LUGO_FRAME_HEADER) return -1;
    *out_type = data[0];
    *payload = data + LUGO_FRAME_HEADER;
    *payload_len = size;
    return 0;
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

// Locate the value for top-level "key"; returns pointer to first value char or NULL.
const char *lugo_json_find(const char *json, const char *key) {
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
void lugo_json_get_string(const char *json, const char *key, char *out, size_t cap) {
    if (cap == 0) return;
    out[0] = '\0';
    const char *p = lugo_json_find(json, key);
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

// Strict counterpart of lugo_json_get_string: refuses to truncate.
int lugo_json_get_string_strict(const char *json, const char *key,
                                char *out, size_t cap) {
    if (cap == 0) return -1;
    out[0] = '\0';
    const char *p = lugo_json_find(json, key);
    if (!p || *p != '"') return -1;
    p++;
    size_t o = 0;
    while (*p != '"') {
        if (!*p) return -1;   // ran off the end: no closing quote
        char c = *p;
        if (c == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'n': c = '\n'; break; case 't': c = '\t'; break;
                case 'r': c = '\r'; break; case 'b': c = '\b'; break;
                case 'f': c = '\f'; break; default: c = *p; break;
            }
        }
        if (o >= cap - 1) { out[0] = '\0'; return -1; }   // would truncate
        out[o++] = c;
        p++;
    }
    out[o] = '\0';
    return 0;
}

// Read integer value for key; returns 0 if absent/non-numeric.
int lugo_json_get_int(const char *json, const char *key) {
    const char *p = lugo_json_find(json, key);
    if (!p) return 0;
    char *end;
    long v = strtol(p, &end, 10);
    return end == p ? 0 : (int)v;
}

// Read boolean value for key ("true"/"false" literal at the value position);
// default_val if the key is absent.
int lugo_json_get_bool(const char *json, const char *key, int default_val) {
    const char *p = lugo_json_find(json, key);
    if (!p) return default_val;
    if (!strncmp(p, "true", 4)) return 1;
    if (!strncmp(p, "false", 5)) return 0;
    return default_val;
}

// Append src to buf at *o (cap total), JSON-escaping " \ and control chars.
// Returns false on overflow.
static bool append_escaped(char *buf, size_t cap, size_t *o, const char *src) {
    static const char hex[] = "0123456789abcdef";
    for (; *src; src++) {
        unsigned char ch = (unsigned char)*src;
        const char *esc = NULL;
        switch (ch) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n";  break;
            case '\t': esc = "\\t";  break;
            case '\r': esc = "\\r";  break;
        }
        if (esc) {
            if (*o + 2 >= cap) return false;
            buf[(*o)++] = esc[0]; buf[(*o)++] = esc[1];
        } else if (ch < 0x20) {
            if (*o + 6 >= cap) return false;
            buf[(*o)++] = '\\'; buf[(*o)++] = 'u';
            buf[(*o)++] = '0';  buf[(*o)++] = '0';
            buf[(*o)++] = hex[(ch >> 4) & 0xF];
            buf[(*o)++] = hex[ch & 0xF];
        } else {
            if (*o + 1 >= cap) return false;
            buf[(*o)++] = ch;
        }
    }
    return true;
}

int lugo_parse_event(const char *json, lugo_event_t *out) {
    memset(out, 0, sizeof(*out));
    if (*skip_ws(json) != '{') return -1;
    char type[32];
    lugo_json_get_string(json, "type", type, sizeof type);
    if (!strcmp(type, "welcome")) {
        out->type = LUGO_EV_WELCOME;
        out->sample_rate = lugo_json_get_int(json, "sample_rate");   // inside audio_params; flat scan is fine
        out->idle_timeout_s = lugo_json_get_int(json, "idle_timeout_s");
    } else if (!strcmp(type, "stt")) {
        out->type = LUGO_EV_STT;
        lugo_json_get_string(json, "text", out->text, sizeof out->text);
    } else if (!strcmp(type, "tts")) {
        char state[24];
        lugo_json_get_string(json, "state", state, sizeof state);
        if (!strcmp(state, "start")) out->type = LUGO_EV_TTS_START;
        else if (!strcmp(state, "stop")) out->type = LUGO_EV_TTS_STOP;
        else if (!strcmp(state, "sentence_start")) {
            out->type = LUGO_EV_TTS_SENTENCE;
            lugo_json_get_string(json, "text", out->text, sizeof out->text);
        } else out->type = LUGO_EV_UNKNOWN;
    } else if (!strcmp(type, "mcp")) {
        out->type = LUGO_EV_MCP;
        out->mcp_payload = lugo_json_find(json, "payload");
    } else if (!strcmp(type, "goodbye")) {
        out->type = LUGO_EV_GOODBYE;
        lugo_json_get_string(json, "reason", out->text, sizeof out->text);
    } else if (!strcmp(type, "error")) {
        out->type = LUGO_EV_ERROR;
        lugo_json_get_string(json, "message", out->text, sizeof out->text);
    } else {
        out->type = LUGO_EV_UNKNOWN;
    }
    return 0;
}

int lugo_build_wakeup(char *buf, int buflen, int in_sr, int out_sr,
                      int frame_ms) {
    int n = snprintf(buf, buflen,
        "{\"type\":\"wakeup\",\"trigger\":\"button\","
        "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":%d,"
        "\"output_sample_rate\":%d,\"frame_duration\":%d},"
        "\"features\":{\"mcp\":true}}",
        in_sr, out_sr, frame_ms);
    if (n < 0 || n >= buflen) return -1;
    return n;
}

int lugo_build_abort(char *buf, int buflen, const char *reason) {
    int n = snprintf(buf, buflen, "{\"type\":\"abort\",\"reason\":\"%s\"}",
                     reason ? reason : "user");
    if (n < 0 || n >= buflen) return -1;
    return n;
}

int lugo_build_new_session(char *buf, int buflen) {
    // No session_id: the gateway mints the new one. Sending an id here would be
    // a resume request, which is the opposite of what this asks for.
    int n = snprintf(buf, buflen, "{\"type\":\"new_session\"}");
    if (n < 0 || n >= buflen) return -1;
    return n;
}

int lugo_build_text(char *buf, int buflen, const char *text) {
    const char *prefix = "{\"type\":\"text\",\"text\":\"";
    const char *suffix = "\"}";
    size_t o = 0;
    size_t plen = strlen(prefix), slen = strlen(suffix);
    if ((int)(plen + slen) >= buflen) return -1;
    memcpy(buf, prefix, plen); o = plen;
    if (!append_escaped(buf, (size_t)buflen - slen, &o, text)) return -1;
    memcpy(buf + o, suffix, slen); o += slen;
    buf[o] = '\0';
    return (int)o;
}
