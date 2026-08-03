#include "board_common.h"
#include "i2s_fd.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C3

// ESP32-C3 SuperMini: same wiring as the DevKitM-1 build (MAX98357A + INMP441 on
// the single full-duplex I2S, panel auto-detected, wake GPIO0) but on a compact
// board with a KNOWN-WEAK PCB antenna (RX ok, TX crippled — see docs/). Selected
// only via Kconfig.
//
// SuperMini pin notes: GPIO8 = onboard LED (active-low), GPIO9 = BOOT button,
// GPIO2/8/9 strapping, GPIO18/19 = USB (not broken out).
static const i2s_fd_cfg_t fd_cfg = {
    .bclk = 7, .ws = 3, .mic_data = 10, .spk_data = 6,
    .mic_bclk = 1, .mic_ws = 2,   // mic on its own SCK/WS pins -> fan out clock
};
// Named so board_t.reserved_pins below reuses the same constants. bl is -1:
// the panel's LED is tied to 3V3 on this wiring, no GPIO to drive.
#define PIN_DISP_CLK  5   // ssd1306 SCL / st7789 SCLK
#define PIN_DISP_DAT  4   // ssd1306 SDA / st7789 MOSI
#define PIN_DISP_DC  20
#define PIN_DISP_RST 21
#define PIN_BTN_WAKE  0

LUGO_DISPLAY_AUTO(PIN_DISP_CLK, PIN_DISP_DAT, PIN_DISP_DC, PIN_DISP_RST, -1);
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = PIN_BTN_WAKE, .vol_up = -1, .vol_down = -1, .emotion = -1,
};

// Display + buttons (see board_t.reserved_pins). Mic/speaker I2S pins are
// omitted on purpose — the I2S driver reserves those itself.
LUGO_RESERVED_PINS(PIN_DISP_CLK, PIN_DISP_DAT, PIN_DISP_DC, PIN_DISP_RST,
                   PIN_BTN_WAKE);

LUGO_BOARD_REGISTER(board_lugo_c3_supermini) {
    .name        = "lugo-c3-supermini",
    .mic         = &i2s_fd_mic_ops,
    .speaker     = &i2s_fd_speaker_ops,
    .display     = NULL,                  // NULL -> auto-detect (see display_cfg)
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = &fd_cfg,
    .speaker_cfg = &fd_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    LUGO_BOARD_RESERVED_PINS,
};

#endif // CONFIG_IDF_TARGET_ESP32C3
