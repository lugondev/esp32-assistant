#pragma once
#include "board_types.h"   // battery_charge_state_t
#include <stdbool.h>

// Pure, host-testable battery math — no hardware access.

// Approximates remaining charge [0,100] from a single-cell Li-ion terminal
// voltage in millivolts, via linear interpolation over a standard discharge
// curve table (steep drop below ~3400mV, long flat plateau in the middle).
// Clamps outside the pack's usable range (<=3300mV -> 0, >=4200mV -> 100)
// rather than extrapolating into meaningless negative/over-100 values.
int battery_pct_from_millivolts(int mv);

// TP4056's CHRG/STDBY are open-drain, active-low, and mutually exclusive per
// the datasheet (never both active in normal operation). Maps the two pin
// reads to a charge state; the "both active" combination shouldn't happen on
// real hardware, so it's treated as NOT_CHARGING rather than guessing which
// one is right.
battery_charge_state_t battery_charge_state_from_pins(bool chrg_active, bool stdby_active);
