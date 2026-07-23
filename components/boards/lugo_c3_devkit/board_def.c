#include "board.h"
#include "board_i2c_probe.h"
#include "i2s_fd.h"
#include "display_ssd1306.h"
#include "buttons_gpio.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C3

// ESP32-C3-DevKitM-1 (ESP32-C3-MINI-1 module): same peripheral wiring as the
// SuperMini build (MAX98357A + INMP441 on the single full-duplex I2S, SSD1306
// OLED over I2C, wake on GPIO0) but on a proper Espressif module whose antenna
// actually works — the fix for the SuperMini's weak-TX antenna defect.
//
// DevKitM-1 pin notes: GPIO8 = onboard RGB LED (WS2812), GPIO9 = BOOT button,
// GPIO2/8/9 are strapping, GPIO18/19 are native USB (not for peripherals).
// The speaker owns the physical BCLK(7)/WS(3); the mic is on its own SCK(1)/
// WS(2), so the shared clock is fanned out onto them (mic_bclk/mic_ws).
static const i2s_fd_cfg_t fd_cfg = {
    .bclk = 7, .ws = 3, .mic_data = 10, .spk_data = 6,
    .mic_bclk = 1, .mic_ws = 2,   // mic on its own SCK/WS pins -> fan out clock
};
static const display_ssd1306_cfg_t display_cfg = {
    .scl = 5, .sda = 4, .i2c_addr = 0x3C,
};
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = 0, .vol_up = -1, .vol_down = -1, .emotion = -1,
};

// SSD1306 ACKs at 0x3C (sometimes 0x3D) on scl/sda; lets autodetect work, a
// Kconfig force still wins by name.
static bool match(void) {
    return board_i2c_probe(display_cfg.scl, display_cfg.sda, display_cfg.i2c_addr, 50) ||
           board_i2c_probe(display_cfg.scl, display_cfg.sda, 0x3D, 50);
}

LUGO_BOARD_REGISTER(board_lugo_c3_devkit) {
    .name        = "lugo-c3-devkit",
    .mic         = &i2s_fd_mic_ops,
    .speaker     = &i2s_fd_speaker_ops,
    .display     = &display_ssd1306_ops,
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = &fd_cfg,        // both point at the shared full-duplex cfg
    .speaker_cfg = &fd_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    .match       = match,
};

#endif // CONFIG_IDF_TARGET_ESP32C3
