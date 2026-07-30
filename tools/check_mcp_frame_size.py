#!/usr/bin/env python3
"""Fail if the MCP tools/list response no longer fits MCP_FRAME_BUF_SIZE.

Why a script and not a C test: the tool registry is assembled by the linker
("mcp_tool" section, see LUGO_MCP_TOOL) and each tool file pulls in ESP-IDF
headers, so neither can be built on the host. This reproduces
mcp_server.c::write_tools_list byte-for-byte over descriptors parsed from the
real sources instead.

Why bother at all: when the response overruns the buffer, snprintf fails closed
and mcp_dispatch returns -1 — the device advertises NO tools whatsoever, with
nothing in the logs naming the size as the cause. Adding one tool with a wordy
description is enough to do it. This turns that silent cliff into a build-time
failure.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOLS_DIR = ROOT / "components" / "mcp_tools"
WS_HEADER = ROOT / "components" / "ws_client" / "include" / "ws_client.h"

# {"type":"mcp","payload":} plus the NUL that snprintf reserves. ws_client wraps
# the dispatch output into a buffer of the SAME size, so this overhead is what
# actually decides the limit.
WRAPPER_OVERHEAD = len('{"type":"mcp","payload":}') + 1


def _string_literal(text: str) -> str:
    """Concatenate adjacent C string literals, as the compiler does."""
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text))


def parse_tools() -> list[tuple[str, str, list[tuple[str, str]], str]]:
    tools = []
    for path in sorted(TOOLS_DIR.glob("*_tools.c")):
        src = path.read_text()
        for match in re.finditer(r"LUGO_MCP_TOOL\(\w+\)\s*\{(.*?)\n\};", src, re.S):
            body = match.group(1)
            name = re.search(r'\.name\s*=\s*"([^"]*)"', body).group(1)
            desc_match = re.search(r'\.description\s*=\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', body)
            desc = _string_literal(desc_match.group(1)) if desc_match else ""
            confirm = "true" if re.search(r"\.requires_confirm\s*=\s*true", body) else "false"
            props: list[tuple[str, str]] = []
            props_sym = re.search(r"\.props\s*=\s*(\w+)", body)
            if props_sym and props_sym.group(1) != "NULL":
                arr = re.search(re.escape(props_sym.group(1)) + r"\[\]\s*=\s*\{(.*?)\};", src, re.S)
                if arr:
                    for p in re.finditer(r'\{\s*"(\w+)"\s*,\s*MCP_PROP_(\w+?)_T', arr.group(1)):
                        props.append((p.group(1), p.group(2).lower()))
                    for p in re.finditer(r'MCP_PROP_(INT|BOOL|STRING)\(\s*"(\w+)"', arr.group(1)):
                        props.append((p.group(2), p.group(1).lower()))
            tools.append((name, desc, props, confirm))
    return tools


def render_tools_list(tools) -> str:
    """Mirror of mcp_server.c::write_tools_list."""
    out = '{"jsonrpc":"2.0","id":2,"result":{"tools":['
    for i, (name, desc, props, confirm) in enumerate(tools):
        out += "," if i else ""
        out += f'{{"name":"{name}","description":"{desc}",'
        out += '"inputSchema":{"type":"object","properties":{'
        out += ",".join(f'"{pn}":{{"type":"{pt}"}}' for pn, pt in props)
        out += f'}}}},"annotations":{{"requiresConfirm":{confirm}}}}}'
    return out + "]}}"


def buffer_size() -> int:
    m = re.search(r"#define\s+MCP_FRAME_BUF_SIZE\s+(\d+)", WS_HEADER.read_text())
    if not m:
        raise SystemExit("could not find MCP_FRAME_BUF_SIZE in ws_client.h")
    return int(m.group(1))


def main() -> int:
    tools = parse_tools()
    if not tools:
        raise SystemExit("parsed zero tools — the LUGO_MCP_TOOL pattern changed?")
    body = render_tools_list(tools)
    cap = buffer_size()
    wrapped = len(body) + WRAPPER_OVERHEAD
    headroom = cap - wrapped

    print(f"tools: {len(tools)}  tools/list: {len(body)} B  wrapped: {wrapped} B  "
          f"cap: {cap} B  headroom: {headroom} B")
    if headroom < 0:
        print("FAIL: tools/list overruns MCP_FRAME_BUF_SIZE — the device would "
              "advertise NO tools at all. Raise MCP_FRAME_BUF_SIZE in ws_client.h "
              "(it sizes both buffers) or shorten a description.")
        return 1
    if headroom < 256:
        print(f"FAIL: only {headroom} B headroom. That is less than one more tool, "
              "and overrunning it fails silently. Raise MCP_FRAME_BUF_SIZE now "
              "rather than after the next tool breaks MCP.")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
