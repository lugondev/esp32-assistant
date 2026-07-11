#include "board.h"
#include "board_i2c_probe.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "display_st7789.h"
#include "buttons_gpio.h"
#include "tp4056_battery.h"
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

// TP4056 not wired on this board yet — fill in gpio_chrg/gpio_stdby once
// soldered (charging status alone, no ADC needed) and adc_channel/r1_ohms/
// r2_ohms later for a real percentage (see tp4056_battery.h). Then add
// `.battery = &tp4056_battery_ops, .battery_cfg = &battery_cfg,` to the
// LUGO_BOARD_REGISTER block below — nothing else needs to change, since
// main.c/statusbar already read through the battery facade unconditionally.
// static const tp4056_battery_cfg_t battery_cfg = {
//     .gpio_chrg = -1, .gpio_stdby = -1,
//     .adc_channel = -1, .adc_unit = ADC_UNIT_1, .adc_atten = ADC_ATTEN_DB_12,
//     .r1_ohms = 100000, .r2_ohms = 100000,
// };

// Real probe for AA_BOARD_AUTODETECT: the logical inverse of
// lugo-s3-ssd1306's match(). Its scl/sda are these same physical pins
// (used here as SPI sclk/mosi instead) — if an SSD1306 ACKs I2C at
// 0x3C/0x3D there, this board loses; if neither answers, this board wins,
// which makes ST7789 the deterministic default when nothing is wired at
// all (matching this board's original single-board behavior).
static bool match(void) {
    return !(board_i2c_probe(display_cfg.sclk, display_cfg.mosi, 0x3C, 50) ||
              board_i2c_probe(display_cfg.sclk, display_cfg.mosi, 0x3D, 50));
}

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
