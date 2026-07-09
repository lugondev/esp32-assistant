// esp32-assistant/components/mcp_tools/registry.c
#include "mcp_tools.h"

// Boundary symbols of the "mcp_tool" section (see linker.lf).
extern const mcp_tool_desc_t *const _mcp_tool_start[];
extern const mcp_tool_desc_t *const _mcp_tool_end[];

// Proof-of-registration tool for this task; Task 4/5 add the real ones and
// this one can stay (it's a harmless, always-available diagnostic) or be
// deleted once real tools exist — implementer's call, not load-bearing.
static mcp_result_t ping_fn(const char *args) {
    (void)args;
    return mcp_ok_text("pong");
}
LUGO_MCP_TOOL(tool_ping) {
    .name = "self.ping", .description = "Diagnostic: returns pong",
    .props = NULL, .requires_confirm = false, .fn = ping_fn,
};

void mcp_tools_init(void) {
    // No-op: the registry is populated at link time via the linker section;
    // nothing to initialize at runtime. Kept for symmetry with
    // board_detect_and_select().
}

int mcp_tools_dispatch(const char *mcp_payload, char *out_buf, int out_cap) {
    int n = (int)(_mcp_tool_end - _mcp_tool_start);
    return mcp_dispatch(_mcp_tool_start, n, mcp_payload, out_buf, out_cap);
}
