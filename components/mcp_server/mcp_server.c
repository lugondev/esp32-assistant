// esp32-assistant/components/mcp_server/mcp_server.c
#include "mcp_server.h"
#include "lugo_protocol.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

int mcp_arg_int(const char *args_json, const char *name, int fallback) {
    const char *p = lugo_json_find(args_json, name);
    return p ? lugo_json_get_int(args_json, name) : fallback;
}

int mcp_arg_bool(const char *args_json, const char *name, int fallback) {
    return lugo_json_get_bool(args_json, name, fallback);
}

void mcp_arg_string(const char *args_json, const char *name, char *out, size_t cap) {
    lugo_json_get_string(args_json, name, out, cap);
}

static mcp_result_t make_result(bool is_error, const char *fmt, va_list ap) {
    mcp_result_t r;
    r.is_error = is_error;
    vsnprintf(r.text, sizeof r.text, fmt, ap);
    return r;
}

mcp_result_t mcp_ok_text(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    mcp_result_t r = make_result(false, fmt, ap);
    va_end(ap);
    return r;
}

mcp_result_t mcp_err(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    mcp_result_t r = make_result(true, fmt, ap);
    va_end(ap);
    return r;
}

// Append src, JSON-escaping " and \. Returns false on overflow.
static bool append_escaped_str(char *buf, int cap, int *o, const char *src) {
    for (; *src; src++) {
        if (*src == '"' || *src == '\\') {
            if (*o + 2 >= cap) return false;
            buf[(*o)++] = '\\'; buf[(*o)++] = *src;
        } else {
            if (*o + 1 >= cap) return false;
            buf[(*o)++] = *src;
        }
    }
    return true;
}

static int find_tool(const mcp_tool_desc_t *const *tools, int n, const char *name) {
    for (int i = 0; i < n; i++) if (!strcmp(tools[i]->name, name)) return i;
    return -1;
}

// Validate args_json against a tool's declared props. Returns NULL if valid,
// else a static description of the first violation.
static const char *validate_args(const mcp_tool_desc_t *tool, const char *args_json) {
    if (!tool->props) return NULL;
    for (const mcp_prop_t *p = tool->props; p->name; p++) {
        const char *v = lugo_json_find(args_json, p->name);
        if (!v) { if (p->required) return "missing required argument"; continue; }
        if (p->type == MCP_PROP_INT_T) {
            int val = lugo_json_get_int(args_json, p->name);
            if ((p->min != 0 || p->max != 0) && (val < p->min || val > p->max))
                return "argument out of range";
        }
    }
    return NULL;
}

static int write_error(char *out, int cap, int id, const char *message) {
    int o = 0;
    o += snprintf(out + o, cap - o, "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":{\"code\":-1,\"message\":\"", id);
    if (o >= cap) return -1;
    if (!append_escaped_str(out, cap, &o, message)) return -1;
    int n = snprintf(out + o, cap - o, "\"}}");
    if (n < 0 || o + n >= cap) return -1;
    return o + n;
}

static int write_tool_call_result(char *out, int cap, int id, mcp_result_t r) {
    int o = 0;
    o += snprintf(out + o, cap - o,
        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{\"isError\":%s,\"content\":[{\"type\":\"text\",\"text\":\"",
        id, r.is_error ? "true" : "false");
    if (o >= cap) return -1;
    if (!append_escaped_str(out, cap, &o, r.text)) return -1;
    int n = snprintf(out + o, cap - o, "\"}]}}");
    if (n < 0 || o + n >= cap) return -1;
    return o + n;
}

static const char *prop_type_name(mcp_prop_type_t t) {
    switch (t) {
        case MCP_PROP_INT_T: return "integer";
        case MCP_PROP_BOOL_T: return "boolean";
        default: return "string";
    }
}

static int write_tools_list(char *out, int cap, int id,
                            const mcp_tool_desc_t *const *tools, int n) {
    int o = snprintf(out, cap, "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{\"tools\":[", id);
    if (o < 0 || o >= cap) return -1;
    for (int i = 0; i < n; i++) {
        const mcp_tool_desc_t *t = tools[i];
        int w = snprintf(out + o, cap - o,
            "%s{\"name\":\"%s\",\"description\":\"%s\","
            "\"inputSchema\":{\"type\":\"object\",\"properties\":{",
            i ? "," : "", t->name, t->description ? t->description : "");
        if (w < 0 || o + w >= cap) return -1;
        o += w;
        bool first = true;
        for (const mcp_prop_t *p = t->props; p && p->name; p++) {
            w = snprintf(out + o, cap - o, "%s\"%s\":{\"type\":\"%s\"}",
                        first ? "" : ",", p->name, prop_type_name(p->type));
            if (w < 0 || o + w >= cap) return -1;
            o += w; first = false;
        }
        w = snprintf(out + o, cap - o, "}},\"annotations\":{\"requiresConfirm\":%s}}",
                    t->requires_confirm ? "true" : "false");
        if (w < 0 || o + w >= cap) return -1;
        o += w;
    }
    int w = snprintf(out + o, cap - o, "]}}");
    if (w < 0 || o + w >= cap) return -1;
    return o + w;
}

int mcp_dispatch(const mcp_tool_desc_t *const *tools, int n_tools,
                 const char *mcp_payload, char *out_buf, int out_cap) {
    int id = lugo_json_get_int(mcp_payload, "id");
    char method[32];
    lugo_json_get_string(mcp_payload, "method", method, sizeof method);

    if (!strcmp(method, "initialize")) {
        int n = snprintf(out_buf, out_cap,
            "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{\"serverInfo\":"
            "{\"name\":\"LugoDevice\",\"version\":\"1.0.0\"}}}", id);
        return (n < 0 || n >= out_cap) ? -1 : n;
    }
    if (!strcmp(method, "tools/list")) {
        return write_tools_list(out_buf, out_cap, id, tools, n_tools);
    }
    if (!strcmp(method, "tools/call")) {
        const char *params = lugo_json_find(mcp_payload, "params");
        char name[64] = "";
        const char *args = "";
        if (params) {
            lugo_json_get_string(params, "name", name, sizeof name);
            const char *a = lugo_json_find(params, "arguments");
            if (a) args = a;
        }
        int idx = find_tool(tools, n_tools, name);
        if (idx < 0) return write_error(out_buf, out_cap, id, "unknown tool");
        const char *bad = validate_args(tools[idx], args);
        if (bad) return write_error(out_buf, out_cap, id, bad);
        mcp_result_t r = tools[idx]->fn(args);
        return write_tool_call_result(out_buf, out_cap, id, r);
    }
    return write_error(out_buf, out_cap, id, "unknown method");
}
