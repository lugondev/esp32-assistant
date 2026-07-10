idf_component_register(
    SRCS "display_font.c" "display.c" "drivers/st7789.c" "drivers/ssd1306.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd driver esp_driver_i2c board)
