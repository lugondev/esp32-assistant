#include "board.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "display_auto.h"
#include "buttons_gpio.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3

// FOUNDATION / PLACEHOLDER — an ESP32-S3-WROOM board that will additionally
// carry a CAMERA. Fill in the real GPIOs when the board exists. Selected only
// via CONFIG_AA_BOARD_LUGO_S3_WROOM (match() returns false).
//
// CAMERA (TODO): add a `.camera` facade to board_t (a camera_ops_t + camera_cfg,
// modelled on the existing `.battery` facade — NULL when a board has no camera),
// then set `.camera = &esp_camera_ops, .camera_cfg = &camera_cfg` here. Nothing
// in this board_def changes shape otherwise; the camera is just another optional
// capability the board plugs in.
static const i2s_mic_cfg_t mic_cfg = {
    .port = 0, .ws = CONFIG_AA_MIC_WS, .sck = CONFIG_AA_MIC_SCK, .sd = CONFIG_AA_MIC_SD,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1, .bclk = CONFIG_AA_SPK_BCLK, .lrc = CONFIG_AA_SPK_LRC, .din = CONFIG_AA_SPK_DIN,
};
static const display_ssd1306_cfg_t ssd1306_cfg = { .scl = 42, .sda = 41, .i2c_addr = 0x3C };
static const display_st7789_cfg_t  st7789_cfg  = { .sclk = 42, .mosi = 41, .dc = 1, .rst = 2, .bl = 17 };
static const display_auto_cfg_t    display_cfg = { .ssd1306 = &ssd1306_cfg, .st7789 = &st7789_cfg };
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = 47, .vol_up = 40, .vol_down = 39, .emotion = -1,
};

static bool match(void) { return false; }  // opt-in via CONFIG_AA_BOARD_LUGO_S3_WROOM

LUGO_BOARD_REGISTER(board_lugo_s3_wroom) {
    .name        = "lugo-s3-wroom",
    .mic         = &i2s_mic_ops,
    .speaker     = &i2s_speaker_ops,
    .display     = NULL,                  // NULL -> auto-detect (see display_cfg)
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = &mic_cfg,
    .speaker_cfg = &spk_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    .match       = match,
};

#endif // CONFIG_IDF_TARGET_ESP32S3
