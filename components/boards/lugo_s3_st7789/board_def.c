#include "board.h"
#include "audio_i2s.h"
#include "display_st7789.h"
#include "buttons_gpio.h"
#include "sdkconfig.h"

// Pins identical to the pre-refactor firmware.
static const audio_i2s_cfg_t audio_cfg = {
    .mic_ws  = CONFIG_AA_MIC_WS,  .mic_sck = CONFIG_AA_MIC_SCK, .mic_sd  = CONFIG_AA_MIC_SD,
    .spk_bclk = CONFIG_AA_SPK_BCLK, .spk_lrc = CONFIG_AA_SPK_LRC, .spk_din = CONFIG_AA_SPK_DIN,
};
static const display_st7789_cfg_t display_cfg = {
    .sclk = 42, .mosi = 41, .dc = 1, .rst = 2, .bl = 17,
};
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = 47, .vol_up = 40, .vol_down = 39,
};

// Only board in the build today, so it is Kconfig-forced and match() is never
// consulted. When a second board is added, replace this with an I2C probe
// (e.g. `return !i2c_probe(ES8311_ADDR);`) so a single binary can auto-detect.
static bool match(void) { return true; }

LUGO_BOARD_REGISTER(board_lugo_s3_st7789) {
    .name        = "lugo-s3-st7789",
    .audio       = &audio_i2s_ops,
    .display     = &display_st7789_ops,
    .buttons     = &buttons_gpio_ops,
    .audio_cfg   = &audio_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    .match       = match,
};
