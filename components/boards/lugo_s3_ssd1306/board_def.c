#include "board.h"
#include "board_i2c_probe.h"
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
    // See lugo_s3_st7789's note on GPIO46 being a strapping pin.
    .wake = 47, .vol_up = 40, .vol_down = 39, .emotion = 46,
};

// Real probe for AA_BOARD_AUTODETECT: an SSD1306 ACKs I2C at 0x3C or 0x3D
// on the same scl/sda pins display_cfg uses below. lugo-s3-st7789's
// match() is the logical inverse of this same check (see that file), so
// exactly one of the two S3 boards matches regardless of link/registration
// order.
static bool match(void) {
    return board_i2c_probe(display_cfg.scl, display_cfg.sda, display_cfg.i2c_addr, 50) ||
           board_i2c_probe(display_cfg.scl, display_cfg.sda, 0x3D, 50);
}

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
