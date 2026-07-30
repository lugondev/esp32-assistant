// esp32-assistant/components/mcp_tools/session_tools.c
#include "mcp_tools.h"

// Requested by the tool, sent by the caller: main.c drains this AFTER writing
// the tool result to the socket. A hook that sent the frame from here would send
// it FIRST, and `new_session` rotates the conversation server-side -- it would
// land while the model is still waiting for this very tool's result, so the
// result reaches a turn the gateway has already moved on from and the assistant
// never gets to confirm. rpi-assistant defers it the same way, for the same
// reason (a2a_client/service.py, `_new_session_requested`).
static bool s_new_session_requested = false;

bool mcp_tools_take_new_session_request(void) {
    bool requested = s_new_session_requested;
    s_new_session_requested = false;
    return requested;
}

static mcp_result_t new_session_fn(const char *args) {
    (void)args;
    s_new_session_requested = true;
    return mcp_ok_text("starting a new conversation");
}

// requires_confirm stays false: nothing is destroyed. The conversation just
// ended is kept server-side (it appears in History and its memories are
// extracted) — this only stops the assistant carrying it forward. Confirming
// every "let's start over" out loud would be noise.
LUGO_MCP_TOOL(tool_new_session) {
    .name = "self.session.new",
    .description = "Start a brand-new conversation: end the current one and forget everything said in it. Use ONLY when the user explicitly asks to start over or forget the conversation so far. Never call it to tidy up or because the topic changed",
    .props = NULL, .requires_confirm = false, .fn = new_session_fn,
};
