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
    int v = mcp_arg_int(args, "volume", -1);
    if (v < 0) return mcp_err("missing volume");
    audio_set_volume(v);
    if (s_volume_hook) s_volume_hook(v);
    return mcp_ok_text("volume set to %d", v);
}
static const mcp_prop_t set_volume_props[] = { MCP_PROP_INT("volume", 0, 100), MCP_PROP_END };
LUGO_MCP_TOOL(tool_set_volume) {
    .name = "self.audio.set_volume", .description = "Set speaker volume (0-100)",
    .props = set_volume_props, .requires_confirm = false, .fn = set_volume_fn,
};
