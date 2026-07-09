#pragma once
#include "esp_err.h"
#include "lugo_protocol.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*ws_event_cb_t)(const lugo_event_t *ev);
typedef void (*ws_audio_cb_t)(const uint8_t *opus, int len);

// Connect to WS /v1/lugo/stream and, on connect, send the Lugo `wakeup`
// handshake declaring `profile` + audio params. Downlink audio arrives v3-framed
// and is delivered (opus payload only) via on_audio; JSON events via on_event.
esp_err_t ws_client_start(const char *host, int port, bool secure,
                          const char *profile, int in_sr, int out_sr, int frame_ms,
                          ws_event_cb_t on_event, ws_audio_cb_t on_audio);
int  ws_client_send_audio(const uint8_t *opus, int len);  // raw opus uplink
int  ws_client_send_abort(const char *reason);
// Send a pre-built JSON-RPC response object as an mcp frame:
// {"type":"mcp","payload":<json_payload>}. json_payload must already be a
// complete, valid JSON value (mcp_tools_dispatch's output).
int  ws_client_send_mcp(const char *json_payload);
bool ws_client_connected(void);
// Sleep/wake the link. false: stay disconnected after a close (no reconnect
// storm on idle goodbye). true: allow the reconnect task to re-establish (~1s).
void ws_client_set_reconnect(bool enabled);
