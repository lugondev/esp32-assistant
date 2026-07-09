// esp32-assistant/test/test_mcp_server.c
#include "mcp_server.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static int s_last_volume = -1;

static mcp_result_t set_volume_fn(const char *args) {
    int v = mcp_arg_int(args, "volume", -1);
    if (v < 0) return mcp_err("missing volume");
    s_last_volume = v;
    return mcp_ok_text("volume set to %d", v);
}

static mcp_result_t status_fn(const char *args) {
    (void)args;
    return mcp_ok_text("ok");
}

static const mcp_prop_t set_volume_props[] = {
    MCP_PROP_INT("volume", 0, 100), MCP_PROP_END,
};

static const mcp_tool_desc_t set_volume_tool = {
    .name = "self.audio.set_volume", .description = "Set speaker volume (0-100)",
    .props = set_volume_props, .requires_confirm = false, .fn = set_volume_fn,
};
static const mcp_tool_desc_t status_tool = {
    .name = "self.get_device_status", .description = "Read device status",
    .props = NULL, .requires_confirm = false, .fn = status_fn,
};

static const mcp_tool_desc_t *const s_tools[] = { &set_volume_tool, &status_tool };

static void test_initialize(void) {
    char out[256];
    int n = mcp_dispatch(s_tools, 2,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}", out, sizeof out);
    CHECK(n > 0);
    CHECK(strstr(out, "\"id\":1") != NULL);
    CHECK(strstr(out, "\"result\"") != NULL);
}

static void test_tools_list_lists_both(void) {
    char out[512];
    int n = mcp_dispatch(s_tools, 2,
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}", out, sizeof out);
    CHECK(n > 0);
    CHECK(strstr(out, "\"id\":2") != NULL);
    CHECK(strstr(out, "self.audio.set_volume") != NULL);
    CHECK(strstr(out, "self.get_device_status") != NULL);
    CHECK(strstr(out, "\"volume\"") != NULL);   // inputSchema property surfaced
}

static void test_tools_call_dispatches_and_returns_result_text(void) {
    char out[256];
    int n = mcp_dispatch(s_tools, 2,
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"self.audio.set_volume\",\"arguments\":{\"volume\":70}}}",
        out, sizeof out);
    CHECK(n > 0);
    CHECK(s_last_volume == 70);
    CHECK(strstr(out, "\"id\":3") != NULL);
    CHECK(strstr(out, "volume set to 70") != NULL);
}

static void test_tools_call_missing_required_arg_is_error_without_calling_fn(void) {
    s_last_volume = -1;
    char out[256];
    int n = mcp_dispatch(s_tools, 2,
        "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"self.audio.set_volume\",\"arguments\":{}}}",
        out, sizeof out);
    CHECK(n > 0);
    CHECK(s_last_volume == -1);   // fn never called
    CHECK(strstr(out, "\"error\"") != NULL);
}

static void test_tools_call_out_of_range_is_error(void) {
    char out[256];
    int n = mcp_dispatch(s_tools, 2,
        "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"self.audio.set_volume\",\"arguments\":{\"volume\":999}}}",
        out, sizeof out);
    CHECK(n > 0);
    CHECK(strstr(out, "\"error\"") != NULL);
}

static void test_tools_call_unknown_tool_is_error(void) {
    char out[256];
    int n = mcp_dispatch(s_tools, 2,
        "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\","
        "\"params\":{\"name\":\"nope\",\"arguments\":{}}}", out, sizeof out);
    CHECK(n > 0);
    CHECK(strstr(out, "\"error\"") != NULL);
}

static void test_arg_helpers(void) {
    const char *args = "{\"volume\":42,\"on\":true,\"name\":\"led\"}";
    CHECK(mcp_arg_int(args, "volume", -1) == 42);
    CHECK(mcp_arg_int(args, "missing", -1) == -1);
    CHECK(mcp_arg_bool(args, "on", 0) == 1);
    char s[16];
    mcp_arg_string(args, "name", s, sizeof s);
    CHECK(strcmp(s, "led") == 0);
}

int main(void) {
    test_initialize();
    test_tools_list_lists_both();
    test_tools_call_dispatches_and_returns_result_text();
    test_tools_call_missing_required_arg_is_error_without_calling_fn();
    test_tools_call_out_of_range_is_error();
    test_tools_call_unknown_tool_is_error();
    test_arg_helpers();
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("OK\n");
    return 0;
}
