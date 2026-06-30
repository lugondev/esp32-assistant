idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi ws_protocol ws_client audio opus_codec nvs_flash)
