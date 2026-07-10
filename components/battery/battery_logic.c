#include "battery_logic.h"

// Standard 1S Li-ion discharge curve, sampled every 10%. Voltage sags fast
// at both ends and sits on a long flat plateau in the middle — a straight
// line from 4200mV to 3300mV would read badly wrong (~30% off) around 3.7V,
// which is where the cell spends most of its life.
static const int PCT_POINTS[] = {   0,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100 };
static const int MV_POINTS[]  = {3300, 3610, 3690, 3710, 3740, 3770, 3790, 3840, 3910, 4020, 4200};
#define N_POINTS (int)(sizeof(PCT_POINTS) / sizeof(PCT_POINTS[0]))

int battery_pct_from_millivolts(int mv) {
    if (mv <= MV_POINTS[0]) return PCT_POINTS[0];
    if (mv >= MV_POINTS[N_POINTS - 1]) return PCT_POINTS[N_POINTS - 1];

    for (int i = 1; i < N_POINTS; i++) {
        if (mv <= MV_POINTS[i]) {
            int mv_lo = MV_POINTS[i - 1], mv_hi = MV_POINTS[i];
            int pct_lo = PCT_POINTS[i - 1], pct_hi = PCT_POINTS[i];
            return pct_lo + (mv - mv_lo) * (pct_hi - pct_lo) / (mv_hi - mv_lo);
        }
    }
    return PCT_POINTS[N_POINTS - 1];  // unreachable given the clamps above
}

battery_charge_state_t battery_charge_state_from_pins(bool chrg_active, bool stdby_active) {
    if (chrg_active && !stdby_active) return BATTERY_CHARGING;
    if (!chrg_active && stdby_active) return BATTERY_FULL;
    return BATTERY_NOT_CHARGING;
}
