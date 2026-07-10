#include "board.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "display_ssd1306.h"
#include "buttons_gpio.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3

// Same mic/speaker/buttons wiring as lugo_s3_st7789 — only the panel
// swapped, from ST7789 (SPI, RGB565) to a real 4-pin SSD1306 module
// (VCC/GND/SCL/SDA — I2C, not SPI; there's no DC/RST pin to wire).
static const i2s_mic_cfg_t mic_cfg = {
    .port = 0, .ws = CONFIG_AA_MIC_WS, .sck = CONFIG_AA_MIC_SCK, .sd = CONFIG_AA_MIC_SD,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1, .bclk = CONFIG_AA_SPK_BCLK, .lrc = CONFIG_AA_SPK_LRC, .din = CONFIG_AA_SPK_DIN,
};
static const display_ssd1306_cfg_t display_cfg = {
    .scl = 42, .sda = 41, .i2c_addr = 0x3C,
};
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = 47, .vol_up = 40, .vol_down = 39,
};

// Kconfig-forced like lugo_s3_st7789 — match() is only consulted under
// AA_BOARD_AUTODETECT, where two ESP32-S3 boards both unconditionally
// matching would be ambiguous; pick one explicitly via AA_BOARD_FORCE
// instead (the Kconfig default) when both are compiled into the same build.
static bool match(void) { return true; }

LUGO_BOARD_REGISTER(board_lugo_s3_ssd1306) {
    .name        = "lugo-s3-ssd1306",
    .mic         = &i2s_mic_ops,
    .speaker     = &i2s_speaker_ops,
    .display     = &display_ssd1306_ops,
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = &mic_cfg,
    .speaker_cfg = &spk_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    .match       = match,
};

#endif // CONFIG_IDF_TARGET_ESP32S3
