#pragma once
#include "esp_err.h"
#include "board_types.h"

// App-facing battery API — dispatches to the active board's battery driver.
//
// Unlike audio/display (always present on every supported board today),
// battery hardware is genuinely optional: a board that hasn't wired one sets
// board_t.battery to NULL, and these calls degrade safely without an #ifdef
// at every call site — read_pct() returns -1 (statusbar_render's existing
// "no sensor" convention) and charge_state() returns BATTERY_NOT_CHARGING.
esp_err_t battery_init(void);
int battery_read_pct(void);
battery_charge_state_t battery_charge_state(void);
