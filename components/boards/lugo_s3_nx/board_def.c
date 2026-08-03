#include "board_common.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3

// Lugo S3 NX: for ESP32-S3 N-series modules (N16R8, N8R8, ...). MAX98357A +
// INMP441 on dual I2S (mic I2S_NUM_0, speaker I2S_NUM_1). The panel is
// auto-detected — an SSD1306 (I2C) if one ACKs on the display pins, otherwise
// the ST7789 (SPI on the same physical pins). This one board replaces the old
// lugo-s3-ssd1306 / lugo-s3-st7789 split (they were the same board, only the
// soldered panel differed).
// These were CONFIG_AA_MIC_*/CONFIG_AA_SPK_* until 2026-08-03. Those symbols
// were global, and lugo-s3-wroom read the same ones, so the two boards could
// not be given different audio pins — retuning one silently retuned the other.
// The values below are exactly what those symbols defaulted to, i.e. the NX's
// real wiring, now written where the rest of this board's pins live.
#define PIN_MIC_WS    4
#define PIN_MIC_SCK   5
#define PIN_MIC_SD    6
#define PIN_SPK_BCLK 15
#define PIN_SPK_LRC  16
#define PIN_SPK_DIN   7

static const i2s_mic_cfg_t mic_cfg = {
    .port = 0, .ws = PIN_MIC_WS, .sck = PIN_MIC_SCK, .sd = PIN_MIC_SD,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1, .bclk = PIN_SPK_BCLK, .lrc = PIN_SPK_LRC, .din = PIN_SPK_DIN,
};
// Same physical pins carry either I2C (SSD1306 SCL/SDA) or SPI (ST7789
// SCLK/MOSI) depending on the soldered panel; ST7789 additionally uses dc/rst/bl.
// Named so board_t.reserved_pins below can reuse the same constants instead of
// repeating the numbers — a second hand-written copy is exactly what let the
// reserved list go stale against the board pinout before.
#define PIN_DISP_CLK  42   // ssd1306 SCL / st7789 SCLK
#define PIN_DISP_DAT  41   // ssd1306 SDA / st7789 MOSI
#define PIN_DISP_DC    1
#define PIN_DISP_RST   2
#define PIN_DISP_BL   17
#define PIN_BTN_WAKE     47
#define PIN_BTN_VOL_UP   40
#define PIN_BTN_VOL_DOWN 39
#define PIN_BTN_EMOTION  46

LUGO_DISPLAY_AUTO(PIN_DISP_CLK, PIN_DISP_DAT, PIN_DISP_DC, PIN_DISP_RST, PIN_DISP_BL);
static const buttons_gpio_cfg_t buttons_cfg = {
    // .emotion (GPIO46) is an S3 strapping pin — harmless as an active-low input
    // (pull-up keeps it high; the {GPIO0=1} SPI-boot path ignores it), but don't
    // hold it during reset/flashing.
    .wake = PIN_BTN_WAKE, .vol_up = PIN_BTN_VOL_UP,
    .vol_down = PIN_BTN_VOL_DOWN, .emotion = PIN_BTN_EMOTION,
};

// Display + buttons (see board_t.reserved_pins). The mic/speaker I2S pins are
// deliberately absent: the I2S driver reserves them itself at channel init, so
// esp_gpio_is_reserved() already rejects them.
LUGO_RESERVED_PINS(PIN_DISP_CLK, PIN_DISP_DAT, PIN_DISP_DC, PIN_DISP_RST,
                   PIN_DISP_BL, PIN_BTN_WAKE, PIN_BTN_VOL_UP,
                   PIN_BTN_VOL_DOWN, PIN_BTN_EMOTION);

LUGO_BOARD_REGISTER(board_lugo_s3_nx) {
    .name        = "lugo-s3-nx",
    .mic         = &i2s_mic_ops,
    .speaker     = &i2s_speaker_ops,
    .display     = NULL,                  // NULL → auto-detect (see display_cfg)
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = &mic_cfg,
    .speaker_cfg = &spk_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    LUGO_BOARD_RESERVED_PINS,
};

#endif // CONFIG_IDF_TARGET_ESP32S3
