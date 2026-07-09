idf_component_register(
    SRCS "display_font.c" "display.c" "drivers/st7789.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd driver board)
