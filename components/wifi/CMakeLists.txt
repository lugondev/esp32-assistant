idf_component_register(
    SRCS "wifi_sta.c" "wifi_cfg.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi nvs_flash esp_event)
