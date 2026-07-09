# esp32-assistant/components/mcp_server/CMakeLists.txt
idf_component_register(SRCS "mcp_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lugo_protocol)
