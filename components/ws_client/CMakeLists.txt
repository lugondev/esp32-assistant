idf_component_register(
    SRCS "ws_client.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_websocket_client ws_protocol)
