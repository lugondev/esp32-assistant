idf_component_register(
    SRCS "buttons.c" "drivers/gpio_buttons.c" "button_hold_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES driver board)
