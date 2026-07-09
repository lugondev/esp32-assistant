#pragma once
#include <stdint.h>
#include <stddef.h>

// v3 binary frame: uint8 type; uint8 reserved; uint16 payload_size (big-endian); payload[]
#define LUGO_FRAME_HEADER 4
#define LUGO_FRAME_OPUS   0
#define LUGO_FRAME_JSON   1

// Encode header+payload into out (cap out_cap). Returns total bytes, or -1 on
// overflow or payload > 0xFFFF.
int lugo_frame_encode(uint8_t type, const uint8_t *payload, int len,
                      uint8_t *out, int out_cap);

// Decode a v3 frame in-place. On success returns 0 and sets *out_type,
// *payload (points into data), *payload_len. Returns -1 if data is shorter than
// the header or the declared size doesn't match the actual payload length.
int lugo_frame_decode(const uint8_t *data, int len, uint8_t *out_type,
                      const uint8_t **payload, int *payload_len);

typedef enum {
    LUGO_EV_WELCOME, LUGO_EV_STT, LUGO_EV_TTS_START, LUGO_EV_TTS_SENTENCE,
    LUGO_EV_TTS_STOP, LUGO_EV_MCP, LUGO_EV_GOODBYE, LUGO_EV_ERROR, LUGO_EV_UNKNOWN
} lugo_ev_type_t;

typedef struct {
    lugo_ev_type_t type;
    char text[256];       // stt/sentence text, error message, or goodbye reason
    int  sample_rate;     // welcome: audio_params.sample_rate
    int  idle_timeout_s;  // welcome
    // For LUGO_EV_MCP: points at the "payload" object's '{' inside the caller's
    // own JSON buffer (borrowed, not copied — same convention as
    // lugo_frame_decode's payload pointer). NULL for all other event types.
    const char *mcp_payload;
} lugo_event_t;

// Parse a Lugo text frame. Returns 0 on success (type=UNKNOWN for unrecognized),
// -1 if the payload isn't a JSON object.
int lugo_parse_event(const char *json, lugo_event_t *out);

// Find the value for top-level "key" in a flat (non-nested-search) scan;
// returns a pointer to the first character of the value, or NULL. Works for
// any JSON value type (object, string, number, bool) — callers combine this
// with lugo_json_get_* or their own object-scoped calls.
const char *lugo_json_find(const char *json, const char *key);

// Copy the string value for key into out (cap-bounded, common escapes
// unescaped). out is "" if the key is absent or not a string.
void lugo_json_get_string(const char *json, const char *key, char *out, size_t cap);

// Read the integer value for key; 0 if absent/non-numeric.
int lugo_json_get_int(const char *json, const char *key);

// Read the boolean value for key ("true"/"false" literal at the value
// position); default_val if the key is absent.
int lugo_json_get_bool(const char *json, const char *key, int default_val);

// Builders. Return bytes written (excluding NUL), or -1 on overflow.
int lugo_build_wakeup(char *buf, int buflen, const char *profile,
                      int in_sr, int out_sr, int frame_ms);
int lugo_build_abort(char *buf, int buflen, const char *reason);
int lugo_build_text(char *buf, int buflen, const char *text);
