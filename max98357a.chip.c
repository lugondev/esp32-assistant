/*
 * MAX98357A I2S DAC/amplifier — Wokwi custom-chip PIN STUB.
 *
 * Wokwi's chip API (wokwi-api.h) has no I2S support, so this chip cannot
 * decode BCLK/LRC framing or produce audio. It exists only to carry the
 * correct pin names so diagram.json wiring can be checked visually, and
 * to log once when the firmware starts clocking BCLK (i.e. confirms the
 * I2S peripheral is actually running).
 */
#include "wokwi-api.h"
#include <stdio.h>

static bool s_i2s_active_logged = false;

static void on_bclk_edge(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  (void)value;
  if (!s_i2s_active_logged) {
    s_i2s_active_logged = true;
    printf("[MAX98357A] BCLK toggling -- I2S audio active\n");
  }
}

void chip_init(void) {
  pin_init("VIN", INPUT);
  pin_init("GND", INPUT);
  pin_init("SD", INPUT);
  pin_init("GAIN", INPUT);
  pin_init("DIN", INPUT);
  pin_init("LRC", INPUT);

  pin_t bclk = pin_init("BCLK", INPUT);
  const pin_watch_config_t bclk_watch = {
    .edge = BOTH,
    .pin_change = on_bclk_edge,
    .user_data = NULL,
  };
  pin_watch(bclk, &bclk_watch);
}
