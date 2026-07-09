#include "board.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "display_st7789.h"
#include "buttons_gpio.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3

// Pins identical to the pre-refactor firmware. Mic on I2S_NUM_0, speaker on I2S_NUM_1.
static const i2s_mic_cfg_t mic_cfg = {
    .port = 0, .ws = CONFIG_AA_MIC_WS, .sck = CONFIG_AA_MIC_SCK, .sd = CONFIG_AA_MIC_SD,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1, .bclk = CONFIG_AA_SPK_BCLK, .lrc = CONFIG_AA_SPK_LRC, .din = CONFIG_AA_SPK_DIN,
};
static const display_st7789_cfg_t display_cfg = {
    .sclk = 42, .mosi = 41, .dc = 1, .rst = 2, .bl = 17,
};
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = 47, .vol_up = 40, .vol_down = 39,
};

static bool match(void) { return true; }   // Kconfig-forced; single S3 board

LUGO_BOARD_REGISTER(board_lugo_s3_st7789) {
    .name        = "lugo-s3-st7789",
    .mic         = &i2s_mic_ops,
    .speaker     = &i2s_speaker_ops,
    .display     = &display_st7789_ops,
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = &mic_cfg,
    .speaker_cfg = &spk_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    .match       = match,
};

#endif // CONFIG_IDF_TARGET_ESP32S3
