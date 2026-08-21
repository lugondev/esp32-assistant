#pragma once
#include "board_types.h"

typedef struct {
    // Active-low GPIOs, one per button_id_t, in enum order. A negative
    // value means "this board doesn't have that button" — the driver skips
    // it entirely (no gpio_config, no polling).
    int wake, vol_up, vol_down, emotion;
    // Optional SECOND GPIO that behaves exactly like `wake`: same press event
    // (BTN_WAKE), same 10s hold-to-setup-portal tracking, held-state tracked
    // independently so the two never interfere. For boards that carry an
    // on-board BOOT button worth pressing into service alongside (or instead
    // of) the soldered wake wire.
    //
    // MUST be set explicitly on every board — a designated initializer that
    // omits it leaves it 0, and 0 is a real GPIO (the BOOT button on most
    // ESP32 boards), so the driver would poll a pin the board never wired.
    // Write `.wake2 = -1` when there is no second wake button.
    int wake2;
} buttons_gpio_cfg_t;

extern const buttons_ops_t buttons_gpio_ops;
