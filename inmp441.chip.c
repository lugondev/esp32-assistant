/*
 * INMP441 I2S MEMS microphone — Wokwi custom-chip PIN STUB.
 *
 * Wokwi's chip API (wokwi-api.h) has no I2S support, so this chip cannot
 * decode WS/SCK framing or produce real PCM samples on SD. It exists only
 * to carry the correct pin names so diagram.json wiring can be checked
 * visually, and to log once when the firmware starts clocking WS (i.e.
 * confirms the I2S peripheral is actually running).
 */
#include "wokwi-api.h"
#include <stdio.h>

static bool s_i2s_active_logged = false;

static void on_ws_edge(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  (void)value;
  if (!s_i2s_active_logged) {
    s_i2s_active_logged = true;
    printf("[INMP441] WS toggling -- I2S mic clock active\n");
  }
}

void chip_init(void) {
  pin_init("VDD", INPUT);
  pin_init("GND", INPUT);
  pin_init("L_R", INPUT);
  pin_t sck = pin_init("SCK", INPUT);
  pin_t sd = pin_init("SD", OUTPUT_LOW);
  (void)sck;
  (void)sd;

  pin_t ws = pin_init("WS", INPUT);
  const pin_watch_config_t ws_watch = {
    .edge = BOTH,
    .pin_change = on_ws_edge,
    .user_data = NULL,
  };
  pin_watch(ws, &ws_watch);
}
