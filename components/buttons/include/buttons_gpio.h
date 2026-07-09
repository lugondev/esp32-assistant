#pragma once
#include "board_types.h"

typedef struct {
    int wake, vol_up, vol_down;   // active-low GPIOs, one per button_id_t
} buttons_gpio_cfg_t;

extern const buttons_ops_t buttons_gpio_ops;
