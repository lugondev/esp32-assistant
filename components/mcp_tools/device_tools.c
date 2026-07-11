// esp32-assistant/components/mcp_tools/device_tools.c
#include "mcp_tools.h"
#include "audio.h"
#include "esp_system.h"

static mcp_result_t status_fn(const char *args) {
    (void)args;
    return mcp_ok_text(
        "heap=%lu volume=%d",
        (unsigned long)esp_get_free_heap_size(), audio_get_volume());
}
LUGO_MCP_TOOL(tool_get_status) {
    .name = "self.get_device_status", .description = "Read free heap and current volume",
    .props = NULL, .requires_confirm = false, .fn = status_fn,
};

// Set by main.c at startup (mcp_tools_set_idle_hook) so idle_fn can drive the
// real FSM transition instead of only answering the LLM. NULL until main.c
// registers it, and in host tests, which is safe: idle_fn degrades to the old
// answer-only behavior.
static void (*s_idle_hook)(void) = NULL;

void mcp_tools_set_idle_hook(void (*cb)(void)) { s_idle_hook = cb; }

static mcp_result_t idle_fn(const char *args) {
    (void)args;
    if (s_idle_hook) s_idle_hook();
    return mcp_ok_text("going idle");
}
LUGO_MCP_TOOL(tool_idle) {
    .name = "self.device.idle", .description = "Tell the device to go idle/rest",
    .props = NULL, .requires_confirm = false, .fn = idle_fn,
};

static mcp_result_t shutdown_fn(const char *args) {
    (void)args;
    // esp_restart() is used instead of true power-off (no PMIC/latch on this
    // board to cut power from software) — closest available "shut down"
    // primitive. Revisit if a board gains a power-latch GPIO.
    esp_restart();
    return mcp_ok_text("restarting");  // unreachable; kept for a valid return path
}
LUGO_MCP_TOOL(tool_shutdown) {
    .name = "self.device.shutdown", .description = "Power off / restart the device",
    .props = NULL, .requires_confirm = true, .fn = shutdown_fn,
};
