// esp32-assistant/components/mcp_tools/session_tools.c
#include "mcp_tools.h"

// Set by main.c at startup (mcp_tools_set_new_session_hook) so the tool sends
// the real `new_session` frame instead of only answering the LLM. NULL until
// main.c registers it, and in host tests — same dependency-inversion pattern as
// the idle and volume hooks (mcp_tools must not depend on ws_client/main).
static void (*s_new_session_hook)(void) = NULL;

void mcp_tools_set_new_session_hook(void (*cb)(void)) { s_new_session_hook = cb; }

static mcp_result_t new_session_fn(const char *args) {
    (void)args;
    if (s_new_session_hook) s_new_session_hook();
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
