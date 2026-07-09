# Adding a hardware tool

A tool is one `LUGO_MCP_TOOL` definition — no other file needs to change.

    static mcp_result_t my_fn(const char *args) {
        int v = mcp_arg_int(args, "some_int", -1);
        if (v < 0) return mcp_err("missing some_int");
        // ... touch hardware here, via board_active()->X for anything that
        // differs per board, or a direct ESP-IDF call for generic MCU
        // peripherals (like gpio_tools.c does) ...
        return mcp_ok_text("did the thing with %d", v);
    }
    static const mcp_prop_t my_props[] = {
        MCP_PROP_INT("some_int", 0, 100), MCP_PROP_END,
    };
    LUGO_MCP_TOOL(tool_my_thing) {
        .name = "self.my.thing",
        .description = "One sentence the LLM sees when deciding whether to call this",
        .props = my_props,
        .requires_confirm = false,
        .fn = my_fn,
    };

Add the new .c file to `mcp_tools/CMakeLists.txt`'s `SRCS` list (not globbed —
see the CMakeLists comment on WHOLE_ARCHIVE for why explicit SRCS, not a glob
across multiple directories, was chosen here unlike `components/boards`).

## Property types

`MCP_PROP_INT(name, min, max)`, `MCP_PROP_BOOL(name)`, `MCP_PROP_STRING(name)`
— all required by default. For an optional property, write the struct literal
directly with `.required = false` (see `display_tools.c`'s `line2` for an
example). `mcp_dispatch` rejects a `tools/call` with a missing required
property or an out-of-range int **before** calling your `fn` — you never need
to re-validate what your `props` array already declares.

## When to set `requires_confirm = true`

Set it when the action is destructive, hard to reverse, or safety-relevant —
`self.device.shutdown` (powers off), `self.gpio.set` (could physically drive
something unexpected). Do **not** set it for read-only or easily-reversible
actions — `self.get_device_status`, `self.audio.set_volume`,
`self.screen.set_backlight`, `self.device.idle` ("go rest" is not destructive).

When `requires_confirm` is true, the **gateway** (not this firmware) injects a
`confirm` boolean into the tool's schema and blocks the first call until the
LLM re-calls with `confirm:true` after asking the user out loud — see
`docs/superpowers/specs/2026-07-09-device-mcp-hardware-tools-design.md`. This
firmware never sees an unconfirmed call; by the time `fn` runs, confirmation
already happened.

## Reserved GPIO pins

If your tool drives a raw GPIO (not through an existing board op), check it
against the reserved-pin list in `gpio_tools.c` first — mic/speaker/display/
button pins must never be reconfigured by a tool call.
