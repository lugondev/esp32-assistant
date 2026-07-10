idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi lugo_protocol ws_client audio opus_codec nvs_flash provisioning display voice buttons board boards robot_eyes gfx mcp_tools statusbar battery)
