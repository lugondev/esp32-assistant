#include "board.h"
#include "board_i2c_probe.h"
#include "i2s_fd.h"
#include "display_ssd1306.h"
#include "buttons_gpio.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C3

// ESP32-C3 SuperMini: MAX98357A (speaker) + INMP441 (mic) on the single
// full-duplex I2S, SSD1306 0.96" OLED over I2C, wake button on GPIO0.
//
// SuperMini pin notes: header exposes GPIO 0-10, 20, 21. GPIO18/19 are USB
// D-/D+ (not broken out). GPIO8 = onboard LED, GPIO9 = BOOT button, GPIO2 =
// strapping — GPIO8/9 left free, GPIO2 used for the mic WS (acceptable for a
// prototype: it's an output into the mic, nothing drives it at reset).
//
// The speaker owns the physical BCLK(7)/WS(3); the mic is wired to its own
// SCK(1)/WS(2), so the shared clock is fanned out onto them (mic_bclk/mic_ws).
static const i2s_fd_cfg_t fd_cfg = {
    .bclk = 7, .ws = 3, .mic_data = 10, .spk_data = 6,
    .mic_bclk = 1, .mic_ws = 2,   // mic on its own SCK/WS pins → fan out clock
};
static const display_ssd1306_cfg_t display_cfg = {
    .scl = 5, .sda = 4, .i2c_addr = 0x3C,
};
static const buttons_gpio_cfg_t buttons_cfg = {
    // Only wake is wired so far (GPIO0, RTC-wake capable). Volume via TTP223 is
    // deferred; the C3 has no emotion button.
    .wake = 0, .vol_up = -1, .vol_down = -1, .emotion = -1,
};

// An SSD1306 ACKs I2C at 0x3C (sometimes 0x3D) on scl/sda — same probe the S3
// SSD1306 board uses. Lets autodetect work; a Kconfig force still wins by name.
static bool match(void) {
    return board_i2c_probe(display_cfg.scl, display_cfg.sda, display_cfg.i2c_addr, 50) ||
           board_i2c_probe(display_cfg.scl, display_cfg.sda, 0x3D, 50);
}

LUGO_BOARD_REGISTER(board_lugo_c3_supermini) {
    .name        = "lugo-c3-supermini",
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
