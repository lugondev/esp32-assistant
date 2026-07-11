idf_component_register(
    SRCS "board_active.c" "board_select.c" "board.c" "board_i2c_probe.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c)
