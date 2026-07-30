#pragma once
#include "esp_err.h"
#include "lugo_protocol.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*ws_event_cb_t)(const lugo_event_t *ev);
typedef void (*ws_audio_cb_t)(const uint8_t *opus, int len);

// Shared size for the two static buffers that carry one mcp_tools_dispatch()
// response end-to-end: main.c's `resp` (raw dispatch output) and ws_client.c's
// `buf` (the {"type":"mcp","payload":...} wrapper built from `resp`). Both
// must stay in lock-step, or the smaller one silently drops the frame (dispatch
// / snprintf both fail closed rather than overflow).
//
// Measured tools/list responses: 7 tools ~1416 bytes; 8 tools (adding
// self.session.new) 1850 bytes, 1876 once ws_client wraps it. That left only
// 172 bytes under the old 2048 cap -- one more tool, or one longer description,
// and dispatch would fail closed and the device would advertise NO tools at all,
// with nothing in the logs pointing at the size. 3072 restores real headroom.
//
// This grows two static buffers, so the cost is ~2 KB of SRAM, not flash. If you
// add a tool, re-measure rather than assuming: the failure mode is silent.
#define MCP_FRAME_BUF_SIZE 3072

// Connect to WS /v1/lugo/stream and, on connect, send the Lugo `wakeup`
// handshake declaring `profile` + audio params. Downlink audio arrives v3-framed
// and is delivered (opus payload only) via on_audio; JSON events via on_event.
// device_token (may be NULL/empty) is sent as the ?device_token= query param
// required by the gateway once auth is enabled (see CONFIG_AA_DEVICE_TOKEN).
esp_err_t ws_client_start(const char *host, int port, bool secure,
                          const char *profile, const char *device_token,
                          int in_sr, int out_sr, int frame_ms,
                          ws_event_cb_t on_event, ws_audio_cb_t on_audio);
int  ws_client_send_audio(const uint8_t *opus, int len);  // raw opus uplink
int  ws_client_send_abort(const char *reason);
// End the current conversation and start a fresh one WITHOUT dropping the
// socket. Needed because this device never disconnects on its own while
// awake: without it, everything it ever says accumulates into one
// server-side conversation (see docs/api.md, `new_session`).
int  ws_client_send_new_session(void);
// Send a pre-built JSON-RPC response object as an mcp frame:
// {"type":"mcp","payload":<json_payload>}. json_payload must already be a
// complete, valid JSON value (mcp_tools_dispatch's output).
int  ws_client_send_mcp(const char *json_payload);
bool ws_client_connected(void);
// Sleep/wake the link. false: stay disconnected after a close (no reconnect
// storm on idle goodbye). true: allow the reconnect task to re-establish (~1s).
void ws_client_set_reconnect(bool enabled);
// HTTP status of a rejected WS handshake since the last CONNECTED (e.g. 403
// when the device_token was revoked), or 0 if none. Lets main.c distinguish
// "server rejected our auth" from a plain network drop after the link ends.
int ws_client_last_handshake_status(void);
