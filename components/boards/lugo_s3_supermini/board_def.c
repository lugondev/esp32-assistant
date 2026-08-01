#include "board_common.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3

// FOUNDATION / PLACEHOLDER — a compact S3 board (its own pinout, distinct from
// the devkit). Fill in the real GPIOs when the board exists, then it works like
// every other board: panel auto-detected, mic/speaker/buttons as declared.
// Selected only via CONFIG_AA_BOARD_LUGO_S3_SUPERMINI (match() returns false),
// so the devkit stays the S3 autodetect default.
static const i2s_mic_cfg_t mic_cfg = {
    .port = 0, .ws = CONFIG_AA_MIC_WS, .sck = CONFIG_AA_MIC_SCK, .sd = CONFIG_AA_MIC_SD,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1, .bclk = CONFIG_AA_SPK_BCLK, .lrc = CONFIG_AA_SPK_LRC, .din = CONFIG_AA_SPK_DIN,
};
// PLACEHOLDER pins (copied from lugo-s3-nx). Named so board_t.reserved_pins
// below reuses the same constants — when the real pinout lands, changing these
// defines updates the cfg structs AND the reserved list together.
#define PIN_DISP_CLK  42   // ssd1306 SCL / st7789 SCLK
#define PIN_DISP_DAT  41   // ssd1306 SDA / st7789 MOSI
#define PIN_DISP_DC    1
#define PIN_DISP_RST   2
#define PIN_DISP_BL   17
#define PIN_BTN_WAKE     47
#define PIN_BTN_VOL_UP   40
#define PIN_BTN_VOL_DOWN 39

LUGO_DISPLAY_AUTO(PIN_DISP_CLK, PIN_DISP_DAT, PIN_DISP_DC, PIN_DISP_RST, PIN_DISP_BL);
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = PIN_BTN_WAKE, .vol_up = PIN_BTN_VOL_UP,
    .vol_down = PIN_BTN_VOL_DOWN, .emotion = -1,
};

// Display + buttons (see board_t.reserved_pins). Mic/speaker I2S pins are
// omitted on purpose — the I2S driver reserves those itself.
LUGO_RESERVED_PINS(PIN_DISP_CLK, PIN_DISP_DAT, PIN_DISP_DC, PIN_DISP_RST,
                   PIN_DISP_BL, PIN_BTN_WAKE, PIN_BTN_VOL_UP,
                   PIN_BTN_VOL_DOWN);

static bool match(void) { return false; }  // opt-in via CONFIG_AA_BOARD_LUGO_S3_SUPERMINI

LUGO_BOARD_REGISTER(board_lugo_s3_supermini) {
    .name        = "lugo-s3-supermini",
    .mic         = &i2s_mic_ops,
    .speaker     = &i2s_speaker_ops,
    .display     = NULL,                  // NULL -> auto-detect (see display_cfg)
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = &mic_cfg,
    .speaker_cfg = &spk_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    LUGO_BOARD_RESERVED_PINS,
    .match       = match,
};

#endif // CONFIG_IDF_TARGET_ESP32S3
