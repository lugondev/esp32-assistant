#include "board.h"
#include "i2s_fd.h"
#include "display_st7789.h"
#include "buttons_gpio.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C3

// PLACEHOLDER pins — no physical C3 board yet. C3 usable GPIO is 0-10 and 18-21;
// avoid strapping (2,8,9) and SPI-flash (12-17) pins. Set these to the real
// wiring when a board exists. Mic + speaker share the single full-duplex I2S.
static const i2s_fd_cfg_t fd_cfg = {
    .bclk = 4, .ws = 5, .mic_data = 6, .spk_data = 7,
};
static const display_st7789_cfg_t display_cfg = {
    .sclk = 0, .mosi = 1, .dc = 10, .rst = 18, .bl = 19,
};
static const buttons_gpio_cfg_t buttons_cfg = {
    // No emotion button: the C3 has no GPIO46 and no free pin wired for it.
    .wake = 3, .vol_up = 20, .vol_down = 21, .emotion = -1,
};

static bool match(void) { return true; }   // Kconfig-forced; single C3 board

LUGO_BOARD_REGISTER(board_lugo_c3_devkit) {
    .name        = "lugo-c3-devkit",
    .mic         = &i2s_fd_mic_ops,
    .speaker     = &i2s_fd_speaker_ops,
    .display     = &display_st7789_ops,
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = &fd_cfg,        // both point at the shared full-duplex cfg
    .speaker_cfg = &fd_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    .match       = match,
};

#endif // CONFIG_IDF_TARGET_ESP32C3
