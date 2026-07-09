// esp32-assistant/components/mcp_server/include/mcp_server.h
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum { MCP_PROP_INT_T, MCP_PROP_BOOL_T, MCP_PROP_STRING_T } mcp_prop_type_t;

typedef struct {
    const char *name;
    mcp_prop_type_t type;
    int min, max;     // MCP_PROP_INT_T only; 0/0 = unbounded
    bool required;
} mcp_prop_t;

#define MCP_PROP_INT(n, lo, hi) {(n), MCP_PROP_INT_T, (lo), (hi), true}
#define MCP_PROP_BOOL(n)        {(n), MCP_PROP_BOOL_T, 0, 0, true}
#define MCP_PROP_STRING(n)      {(n), MCP_PROP_STRING_T, 0, 0, true}
#define MCP_PROP_END            {NULL, MCP_PROP_INT_T, 0, 0, false}

typedef struct {
    bool is_error;
    char text[192];
} mcp_result_t;

mcp_result_t mcp_ok_text(const char *fmt, ...);
mcp_result_t mcp_err(const char *fmt, ...);

// args_json points at the "arguments" object's '{', or "" if the tool takes
// no arguments (never NULL).
typedef mcp_result_t (*mcp_tool_fn_t)(const char *args_json);

typedef struct {
    const char *name;          // e.g. "self.audio.set_volume"
    const char *description;
    const mcp_prop_t *props;   // NULL or MCP_PROP_END-terminated array
    bool requires_confirm;
    mcp_tool_fn_t fn;
} mcp_tool_desc_t;

int mcp_arg_int(const char *args_json, const char *name, int fallback);
int mcp_arg_bool(const char *args_json, const char *name, int fallback);
void mcp_arg_string(const char *args_json, const char *name, char *out, size_t cap);

// Handle one JSON-RPC request found in mcp_payload (the value pointed to by
// lugo_event_t.mcp_payload). Writes the full JSON-RPC response into out_buf.
// Returns the response length, or -1 if it doesn't fit in out_cap.
int mcp_dispatch(const mcp_tool_desc_t *const *tools, int n_tools,
                 const char *mcp_payload, char *out_buf, int out_cap);
