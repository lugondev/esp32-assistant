idf_component_register(
    SRCS "display_font.c" "display.c" "drivers/st7789.c" "drivers/ssd1306.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "drivers"
    REQUIRES esp_lcd driver esp_driver_i2c board)
