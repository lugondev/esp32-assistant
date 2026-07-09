idf_component_register(
    SRCS "buttons.c" "drivers/gpio_buttons.c"
    INCLUDE_DIRS "include"
    REQUIRES driver board)
