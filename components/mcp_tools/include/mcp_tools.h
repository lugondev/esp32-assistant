// esp32-assistant/components/mcp_tools/include/mcp_tools.h
#pragma once
#include "mcp_server.h"

// Define a hardware tool and auto-register it into the linker "mcp_tool"
// section (mirrors LUGO_BOARD_REGISTER in components/board/include/board.h):
//   LUGO_MCP_TOOL(tool_my_thing) { .name = "self.my.thing", ... };
#define LUGO_MCP_TOOL(sym)                                                \
    static const mcp_tool_desc_t sym;                                    \
    static const mcp_tool_desc_t *const sym##_ref                        \
        __attribute__((used, section("mcp_tool"))) = &sym;               \
    static const mcp_tool_desc_t sym =

// No-op placeholder for future startup logic (kept for symmetry with
// board_detect_and_select); the registry itself needs no init since
// mcp_tools_dispatch walks the linker section directly.
void mcp_tools_init(void);

// Handle one mcp payload against every LUGO_MCP_TOOL-registered tool.
// Target-only (walks the real linker section) — not host-tested.
int mcp_tools_dispatch(const char *mcp_payload, char *out_buf, int out_cap);

// device_tools.c / audio_tools.c own app-level state (s_state, s_active,
// s_status_q, ...) that mcp_tools must not reach into directly — main and
// mcp_tools would form a circular component dependency. Instead main.c
// registers these hooks once at startup (same dependency-inversion pattern as
// buttons_start(on_button)); the tool functions call them if set, so
// self.device.idle / self.audio.set_volume drive the real FSM transition and
// on-screen feedback instead of only answering the LLM.
void mcp_tools_set_idle_hook(void (*cb)(void));
void mcp_tools_set_volume_hook(void (*cb)(int volume));
