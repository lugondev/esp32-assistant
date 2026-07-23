#include "board.h"
#include "i2s_fd.h"
#include "display_auto.h"
#include "buttons_gpio.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C3

// ESP32-C3 SuperMini: same wiring as the DevKitM-1 build (MAX98357A + INMP441 on
// the single full-duplex I2S, panel auto-detected, wake GPIO0) but on a compact
// board with a KNOWN-WEAK PCB antenna (RX ok, TX crippled — see docs/). Selected
// only via Kconfig (match() returns false), so the DevKitM-1 stays the C3 default.
//
// SuperMini pin notes: GPIO8 = onboard LED (active-low), GPIO9 = BOOT button,
// GPIO2/8/9 strapping, GPIO18/19 = USB (not broken out).
static const i2s_fd_cfg_t fd_cfg = {
    .bclk = 7, .ws = 3, .mic_data = 10, .spk_data = 6,
    .mic_bclk = 1, .mic_ws = 2,   // mic on its own SCK/WS pins -> fan out clock
};
static const display_ssd1306_cfg_t ssd1306_cfg = { .scl = 5, .sda = 4, .i2c_addr = 0x3C };
static const display_st7789_cfg_t  st7789_cfg  = { .sclk = 5, .mosi = 4, .dc = 20, .rst = 21, .bl = -1 };
static const display_auto_cfg_t    display_cfg = { .ssd1306 = &ssd1306_cfg, .st7789 = &st7789_cfg };
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = 0, .vol_up = -1, .vol_down = -1, .emotion = -1,
};

static bool match(void) { return false; }  // opt-in via CONFIG_AA_BOARD_LUGO_C3_SUPERMINI

LUGO_BOARD_REGISTER(board_lugo_c3_supermini) {
    .name        = "lugo-c3-supermini",
    .mic         = &i2s_fd_mic_ops,
    .speaker     = &i2s_fd_speaker_ops,
    .display     = NULL,                  // NULL -> auto-detect (see display_cfg)
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = &fd_cfg,
    .speaker_cfg = &fd_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    .match       = match,
};

#endif // CONFIG_IDF_TARGET_ESP32C3
