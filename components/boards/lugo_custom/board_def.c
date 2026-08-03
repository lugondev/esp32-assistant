#include "board_common.h"
#include "sdkconfig.h"

#if CONFIG_AA_BOARD_CUSTOM

// The bring-up board: every pin comes from the "Custom board pinout" Kconfig
// menu, so new hardware needs no new file. Unlike every other board here, this
// one deliberately holds no knowledge — there are no pin numbers to explain and
// no wiring quirks recorded, because it is not one board.
//
// When a pinout entered here is proven, promote it: copy these values into a
// components/boards/<name>/board_def.c and write down what was learned getting
// there. That is how lugo-s3-supermini earned its GPIO4 warning, and it is the
// part a Kconfig menu cannot carry.

#ifdef CONFIG_AA_CUSTOM_MIC_RIGHT_SLOT
#define CUSTOM_MIC_RIGHT_SLOT true
#else
#define CUSTOM_MIC_RIGHT_SLOT false
#endif

#if CONFIG_IDF_TARGET_ESP32S3
#include "i2s_mic.h"
#include "i2s_speaker.h"
// Dual I2S: mic on controller 0, speaker on controller 1 — the split every S3
// board here uses. Not exposed in Kconfig; a custom board has no reason to
// differ, and a knob would only be a way to get it wrong.
static const i2s_mic_cfg_t mic_cfg = {
    .port = 0,
    .ws   = CONFIG_AA_CUSTOM_MIC_WS,
    .sck  = CONFIG_AA_CUSTOM_MIC_SCK,
    .sd   = CONFIG_AA_CUSTOM_MIC_SD,
    .right_slot = CUSTOM_MIC_RIGHT_SLOT,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1,
    .bclk = CONFIG_AA_CUSTOM_SPK_BCLK,
    .lrc  = CONFIG_AA_CUSTOM_SPK_LRC,
    .din  = CONFIG_AA_CUSTOM_SPK_DIN,
};
#define CUSTOM_MIC_OPS     &i2s_mic_ops
#define CUSTOM_SPEAKER_OPS &i2s_speaker_ops
#define CUSTOM_MIC_CFG     &mic_cfg
#define CUSTOM_SPEAKER_CFG &spk_cfg

#elif CONFIG_IDF_TARGET_ESP32C3
#include "i2s_fd.h"
// One I2S controller, shared by RX and TX; both ops back onto this single cfg.
static const i2s_fd_cfg_t fd_cfg = {
    .bclk     = CONFIG_AA_CUSTOM_FD_BCLK,
    .ws       = CONFIG_AA_CUSTOM_FD_WS,
    .mic_data = CONFIG_AA_CUSTOM_FD_MIC_DATA,
    .spk_data = CONFIG_AA_CUSTOM_FD_SPK_DATA,
    .mic_bclk = CONFIG_AA_CUSTOM_FD_MIC_BCLK,
    .mic_ws   = CONFIG_AA_CUSTOM_FD_MIC_WS,
};
#define CUSTOM_MIC_OPS     &i2s_fd_mic_ops
#define CUSTOM_SPEAKER_OPS &i2s_fd_speaker_ops
#define CUSTOM_MIC_CFG     &fd_cfg
#define CUSTOM_SPEAKER_CFG &fd_cfg

#else
#error "lugo-custom supports esp32s3 and esp32c3 only — add an audio branch for this target"
#endif

#if CONFIG_AA_CUSTOM_DISP_AUTO
LUGO_DISPLAY_AUTO(CONFIG_AA_CUSTOM_DISP_CLK, CONFIG_AA_CUSTOM_DISP_DAT,
                  CONFIG_AA_CUSTOM_DISP_DC, CONFIG_AA_CUSTOM_DISP_RST,
                  CONFIG_AA_CUSTOM_DISP_BL);
#define CUSTOM_DISPLAY_CFG &display_cfg
LUGO_RESERVED_PINS(CONFIG_AA_CUSTOM_DISP_CLK, CONFIG_AA_CUSTOM_DISP_DAT,
                   CONFIG_AA_CUSTOM_DISP_DC, CONFIG_AA_CUSTOM_DISP_RST,
                   CONFIG_AA_CUSTOM_DISP_BL,
                   CONFIG_AA_CUSTOM_BTN_WAKE, CONFIG_AA_CUSTOM_BTN_VOL_UP,
                   CONFIG_AA_CUSTOM_BTN_VOL_DOWN, CONFIG_AA_CUSTOM_BTN_EMOTION);

#elif CONFIG_AA_CUSTOM_DISP_SSD1306
// SSD1306 only: display_auto_cfg_t takes .st7789 = NULL, so display_init()
// probes I2C and stops there instead of falling back to a panel that is not
// fitted. DC/RST are not reserved because nothing drives them.
static const display_ssd1306_cfg_t ssd1306_cfg = {
    .scl = CONFIG_AA_CUSTOM_DISP_CLK, .sda = CONFIG_AA_CUSTOM_DISP_DAT,
    .i2c_addr = 0x3C,
};
static const display_auto_cfg_t display_cfg = {
    .ssd1306 = &ssd1306_cfg, .st7789 = NULL,
};
#define CUSTOM_DISPLAY_CFG &display_cfg
LUGO_RESERVED_PINS(CONFIG_AA_CUSTOM_DISP_CLK, CONFIG_AA_CUSTOM_DISP_DAT,
                   CONFIG_AA_CUSTOM_BTN_WAKE, CONFIG_AA_CUSTOM_BTN_VOL_UP,
                   CONFIG_AA_CUSTOM_BTN_VOL_DOWN, CONFIG_AA_CUSTOM_BTN_EMOTION);

#else  // CONFIG_AA_CUSTOM_DISP_NONE
// display_init() checks its cfg for NULL and leaves the headless ops in place.
#define CUSTOM_DISPLAY_CFG NULL
LUGO_RESERVED_PINS(CONFIG_AA_CUSTOM_BTN_WAKE, CONFIG_AA_CUSTOM_BTN_VOL_UP,
                   CONFIG_AA_CUSTOM_BTN_VOL_DOWN, CONFIG_AA_CUSTOM_BTN_EMOTION);
#endif

// A -1 entry in reserved_pins above is inert: no GPIO is -1, so an absent
// button reserves nothing. That is why the button pins are listed
// unconditionally while DC/RST are gated on the panel that uses them.
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake     = CONFIG_AA_CUSTOM_BTN_WAKE,
    .vol_up   = CONFIG_AA_CUSTOM_BTN_VOL_UP,
    .vol_down = CONFIG_AA_CUSTOM_BTN_VOL_DOWN,
    .emotion  = CONFIG_AA_CUSTOM_BTN_EMOTION,
};

LUGO_BOARD_REGISTER(board_lugo_custom) {
    .name        = "lugo-custom",
    .mic         = CUSTOM_MIC_OPS,
    .speaker     = CUSTOM_SPEAKER_OPS,
    .display     = NULL,                  // NULL -> auto-detect (see display_cfg)
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = CUSTOM_MIC_CFG,
    .speaker_cfg = CUSTOM_SPEAKER_CFG,
    .display_cfg = CUSTOM_DISPLAY_CFG,
    .buttons_cfg = &buttons_cfg,
    LUGO_BOARD_RESERVED_PINS,
};

#endif // CONFIG_AA_BOARD_CUSTOM
