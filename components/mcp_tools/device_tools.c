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

static mcp_result_t idle_fn(const char *args) {
    (void)args;
    // Phase 1: no direct FSM hook exists yet from a tool callback context;
    // WS idle-timeout already drives the sleep transition (see
    // [[lugo-device-protocol]] connect-on-wake lifecycle). This tool answers
    // affirmatively so the LLM can say "okay, resting" — the actual
    // WS-level idle/goodbye still governs the real disconnect. Revisit if a
    // direct main.c FSM hook becomes necessary.
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
