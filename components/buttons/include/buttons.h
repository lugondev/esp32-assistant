#pragma once
#include "board_types.h"   // button_id_t

// Configures the buttons (active-low, internal pull-up) and starts a debounced
// polling task that calls on_press(id) once per press. The callback runs in the
// button task's context — keep it light (set flags, queue messages, adjust an
// int); do NOT call blocking SPI/I2S hardware directly from it.
void buttons_start(void (*on_press)(button_id_t id));
