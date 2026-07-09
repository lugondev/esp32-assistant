// esp32-assistant/components/mcp_tools/display_tools.c
#include "mcp_tools.h"
#include "display.h"

static mcp_result_t show_text_fn(const char *args) {
    char line1[64] = "", line2[64] = "";
    mcp_arg_string(args, "line1", line1, sizeof line1);
    mcp_arg_string(args, "line2", line2, sizeof line2);
    display_show(line1, line2[0] ? line2 : NULL);
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
