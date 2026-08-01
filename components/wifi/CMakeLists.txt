idf_component_register(
    SRCS "wifi_sta.c" "wifi_cfg.c" "wifi_signal.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi nvs_flash esp_event
    PRIV_REQUIRES esp_timer)
