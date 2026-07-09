# esp32-assistant/components/mcp_tools/CMakeLists.txt
# WHOLE_ARCHIVE is required, not optional — see components/boards/CMakeLists.txt
# for the full explanation. Each tool file registers purely through a static
# "mcp_tool"-section pointer (LUGO_MCP_TOOL) that nothing references by name;
# without WHOLE_ARCHIVE the linker never pulls that object out of
# libmcp_tools.a, and the section stays empty at runtime.
idf_component_register(SRCS "registry.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mcp_server
                       LDFRAGMENTS "linker.lf"
                       WHOLE_ARCHIVE)
