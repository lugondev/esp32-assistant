// esp32-assistant/components/mcp_tools/registry.c
#include "mcp_tools.h"

// Boundary symbols of the "mcp_tool" section (see linker.lf).
extern const mcp_tool_desc_t *const _mcp_tool_start[];
extern const mcp_tool_desc_t *const _mcp_tool_end[];

// The Task 3 scaffold's self.ping proof tool has been removed now that the
// real v1 tools exist (audio_tools.c, display_tools.c, gpio_tools.c,
// device_tools.c) — nothing left here but the linker-section bookkeeping.

void mcp_tools_init(void) {
    // No-op: the registry is populated at link time via the linker section;
    // nothing to initialize at runtime. Kept for symmetry with
    // board_detect_and_select().
}

int mcp_tools_dispatch(const char *mcp_payload, char *out_buf, int out_cap) {
    int n = (int)(_mcp_tool_end - _mcp_tool_start);
    return mcp_dispatch(_mcp_tool_start, n, mcp_payload, out_buf, out_cap);
}
