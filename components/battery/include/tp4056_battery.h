#pragma once
#include "board_types.h"
#include "hal/adc_types.h"

// battery_ops_t backed by a TP4056 charger module: CHRG/STDBY read as plain
// GPIOs (open-drain, active-low — wire an external pull-up per the TP4056
// datasheet if the module doesn't already have one), and battery percentage
// from a resistor-divider off BAT+ into one ADC channel.
//
// Percentage reading is independent of the charging pins: set adc_channel
// to -1 (or leave this whole struct's ADC fields zeroed) to skip the divider
// read entirely and report read_pct() as -1 (statusbar's "no reading" — see
// battery.h) while charge_state() still works off the GPIOs alone. That's
// the intended first step here: wire CHRG/STDBY now, add the divider later
// without touching any call sites.
typedef struct {
    int gpio_chrg;    // TP4056 CHRG pin — LOW while actively charging
    int gpio_stdby;   // TP4056 STDBY pin — LOW once charge-complete/standby
    int adc_channel;  // ADC channel wired to the divider midpoint; -1 = no ADC reading
    adc_unit_t adc_unit;
    adc_atten_t adc_atten;   // pick so the divided voltage stays under the
                             // attenuation's max input (e.g. ADC_ATTEN_DB_12
                             // for the full ~3.3V range on ESP32-S3)
    int r1_ohms;      // BAT+ --R1-- ADC_PIN --R2-- GND
    int r2_ohms;
} tp4056_battery_cfg_t;

extern const battery_ops_t tp4056_battery_ops;
