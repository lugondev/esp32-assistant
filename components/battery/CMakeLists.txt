idf_component_register(
    SRCS "battery.c" "battery_logic.c" "drivers/tp4056_battery.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_adc board)
