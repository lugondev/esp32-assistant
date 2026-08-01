// esp32-assistant/components/mcp_tools/display_tools.c
#include "mcp_tools.h"
#include "display.h"

// Set by main.c at startup (mcp_tools_set_show_text_hook), for the same reason
// the idle/volume hooks exist — but here it is a correctness requirement, not
// just nicer feedback.
//
// Tool functions run inside mcp_tools_dispatch, which main.c calls from
// on_event(), which runs on the esp_websocket_client's own task. display_show()
// drives the panel directly (SPI transactions on the ST7789, or the shared
// shadow framebuffer + scratch buffer on the SSD1306), and status_task is
// already doing exactly that ~20x/s for the idle eyes. esp_lcd panel handles
// are not thread-safe, so calling it from here interleaved two tasks' SPI
// transactions on one panel — the same class of bug that made the original
// on_event()->display_show() path crash at session start, and the reason
// main.c states that status_task is the ONLY thing allowed to touch the panel.
//
// So the hook queues the text to status_task instead. Without it registered
// (host tests, or a build that forgets to call the setter) the tool reports an
// error rather than falling back to display_show() — the fallback IS the bug.
static void (*s_show_text_hook)(const char *line1, const char *line2) = NULL;

void mcp_tools_set_show_text_hook(void (*cb)(const char *line1, const char *line2)) {
    s_show_text_hook = cb;
}

static mcp_result_t show_text_fn(const char *args) {
    // line1 is 32 to match the status message's own line1 field (main.c's
    // status_msg_t), so a long first line truncates at one known point rather
    // than being cut here at 64 and again on the way to the panel.
    char line1[32] = "", line2[64] = "";
    mcp_arg_string(args, "line1", line1, sizeof line1);
    mcp_arg_string(args, "line2", line2, sizeof line2);
    if (!s_show_text_hook) return mcp_err("screen not available");
    s_show_text_hook(line1, line2[0] ? line2 : NULL);
    return mcp_ok_text("shown");
}
static const mcp_prop_t show_text_props[] = {
    MCP_PROP_STRING("line1"),
    { "line2", MCP_PROP_STRING_T, 0, 0, false },  // optional second line
    MCP_PROP_END,
};
LUGO_MCP_TOOL(tool_show_text) {
    .name = "self.screen.show_text", .description = "Show up to two lines of text on the screen",
    .props = show_text_props, .requires_confirm = false, .fn = show_text_fn,
};

static mcp_result_t set_backlight_fn(const char *args) {
    int on = mcp_arg_bool(args, "on", -1);
    if (on < 0) return mcp_err("missing on");
    display_set_backlight(on != 0);
    return mcp_ok_text(on ? "backlight on" : "backlight off");
}
static const mcp_prop_t set_backlight_props[] = { MCP_PROP_BOOL("on"), MCP_PROP_END };
LUGO_MCP_TOOL(tool_set_backlight) {
    .name = "self.screen.set_backlight", .description = "Turn the screen backlight on or off",
    .props = set_backlight_props, .requires_confirm = false, .fn = set_backlight_fn,
};
