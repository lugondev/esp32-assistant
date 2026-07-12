// esp32-assistant/components/mcp_tools/audio_tools.c
#include "mcp_tools.h"
#include "audio.h"

// Set by main.c at startup (mcp_tools_set_volume_hook) so a voice-driven
// volume change shows the same "Volume NN%" overlay + auto-revert as the
// physical Vol +/- buttons (main.c:on_button), instead of changing the level
// silently. NULL until main.c registers it, and in host tests.
static void (*s_volume_hook)(int) = NULL;

void mcp_tools_set_volume_hook(void (*cb)(int)) { s_volume_hook = cb; }

static mcp_result_t set_volume_fn(const char *args) {
    int v = mcp_arg_int(args, "volume", -1);  // -1 sentinel = not provided
    int d = mcp_arg_int(args, "delta", 0);    // 0 sentinel = not provided (delta=0 is a no-op anyway)
    int new_v;
    if (v >= 0 && d != 0) return mcp_err("provide either volume or delta, not both");
    if (v >= 0) {
        audio_set_volume(v);
        new_v = v;
    } else if (d != 0) {
        new_v = audio_adjust_volume(d);
    } else {
        return mcp_err("missing volume or delta");
    }
    if (s_volume_hook) s_volume_hook(new_v);
    return mcp_ok_text("volume set to %d", new_v);
}
static const mcp_prop_t set_volume_props[] = {
    {"volume", MCP_PROP_INT_T, 0, 100, false},
    {"delta", MCP_PROP_INT_T, -100, 100, false},
    MCP_PROP_END,
};
LUGO_MCP_TOOL(tool_set_volume) {
    .name = "self.audio.set_volume",
    .description = "Set speaker volume: pass volume (0-100 absolute) or delta (e.g. +10/-10 relative) - not both",
    .props = set_volume_props, .requires_confirm = false, .fn = set_volume_fn,
};
