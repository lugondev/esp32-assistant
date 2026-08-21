#pragma once
#include "board_types.h"   // button_id_t

// Configures the buttons (active-low, internal pull-up) and starts a debounced
// polling task that calls on_press(id) once per press. The callback runs in the
// button task's context — keep it light (set flags, queue messages, adjust an
// int); do NOT call blocking SPI/I2S hardware directly from it.
void buttons_start(void (*on_press)(button_id_t id));

// Arms/disarms the erase confirmation. While armed the Wake buttons stop
// reporting taps and the 10s/15s ladder entirely; a 3s hold fires
// BTN_WAKE_ERASE_CONFIRM and anything shorter fires BTN_WAKE_CONFIRM_ABORT.
// Safe to call on a board whose driver has no hold ladder (does nothing).
void buttons_set_confirm_mode(bool on);
