#include "board.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "display_auto.h"
#include "buttons_gpio.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3

// Lugo S3 devkit: MAX98357A + INMP441 on dual I2S (mic I2S_NUM_0, speaker
// I2S_NUM_1). The panel is auto-detected — an SSD1306 (I2C) if one ACKs on the
// display pins, otherwise the ST7789 (SPI on the same physical pins). This one
// board replaces the old lugo-s3-ssd1306 / lugo-s3-st7789 split (they were the
// same board, only the soldered panel differed).
static const i2s_mic_cfg_t mic_cfg = {
    .port = 0, .ws = CONFIG_AA_MIC_WS, .sck = CONFIG_AA_MIC_SCK, .sd = CONFIG_AA_MIC_SD,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1, .bclk = CONFIG_AA_SPK_BCLK, .lrc = CONFIG_AA_SPK_LRC, .din = CONFIG_AA_SPK_DIN,
};
// Same physical pins carry either I2C (SSD1306 SCL/SDA) or SPI (ST7789
// SCLK/MOSI) depending on the soldered panel; ST7789 additionally uses dc/rst/bl.
static const display_ssd1306_cfg_t ssd1306_cfg = { .scl = 42, .sda = 41, .i2c_addr = 0x3C };
static const display_st7789_cfg_t  st7789_cfg  = { .sclk = 42, .mosi = 41, .dc = 1, .rst = 2, .bl = 17 };
static const display_auto_cfg_t    display_cfg = { .ssd1306 = &ssd1306_cfg, .st7789 = &st7789_cfg };
static const buttons_gpio_cfg_t buttons_cfg = {
    // .emotion (GPIO46) is an S3 strapping pin — harmless as an active-low input
    // (pull-up keeps it high; the {GPIO0=1} SPI-boot path ignores it), but don't
    // hold it during reset/flashing.
    .wake = 47, .vol_up = 40, .vol_down = 39, .emotion = 46,
};

static bool match(void) { return true; }  // the S3 autodetect default

LUGO_BOARD_REGISTER(board_lugo_s3_devkit) {
    .name        = "lugo-s3-devkit",
    .mic         = &i2s_mic_ops,
    .speaker     = &i2s_speaker_ops,
    .display     = NULL,                  // NULL → auto-detect (see display_cfg)
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = &mic_cfg,
    .speaker_cfg = &spk_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    .match       = match,
};

#endif // CONFIG_IDF_TARGET_ESP32S3
