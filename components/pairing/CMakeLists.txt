idf_component_register(
    SRCS "pairing_logic.c" "pairing_store.c" "pairing_net.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_http_client display
)
