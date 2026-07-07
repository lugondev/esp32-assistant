idf_component_register(
    SRCS "provisioning_ssid.c" "provisioning_form.c" "provisioning.c"
    INCLUDE_DIRS "include"
    REQUIRES wifi esp_wifi esp_netif esp_http_server nvs_flash lwip display)
