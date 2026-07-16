#pragma once
#include "board_types.h"

typedef struct {
    // Active-low GPIOs, one per button_id_t, in enum order. A negative
    // value means "this board doesn't have that button" — the driver skips
    // it entirely (no gpio_config, no polling).
    int wake, vol_up, vol_down, emotion;
} buttons_gpio_cfg_t;

extern const buttons_ops_t buttons_gpio_ops;
