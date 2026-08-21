#include "board_common.h"
#include "i2s_fd.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C3

// ESP32-C3-DevKitM-1 (ESP32-C3-MINI-1 module): MAX98357A + INMP441 on the single
// full-duplex I2S, panel auto-detected (SSD1306 I2C, else ST7789 SPI on the same
// pins), wake on GPIO0. Proper Espressif module whose antenna works — the fix
// for the SuperMini's weak-TX antenna defect.
//
// DevKitM-1 pin notes: GPIO8 = onboard RGB LED (WS2812), GPIO9 = BOOT button,
// GPIO2/8/9 are strapping, GPIO18/19 are native USB (not for peripherals).
static const i2s_fd_cfg_t fd_cfg = {
    // Two independent simplex channels on the one I2S controller: the speaker
    // (MAX98357A) and mic (INMP441) each get their OWN bclk/ws, wired to
    // separate pins — like xiaozhi's NoAudioCodecSimplex. Sharing one clock
    // (full-duplex) left the C3 RX unsampled (mic read silence).
    .bclk = 7, .ws = 3, .spk_data = 6,          // MAX98357A: BCLK 7, LRC 3, DIN 6
    .mic_bclk = 1, .mic_ws = 2, .mic_data = 10, // INMP441: SCK 1, WS 2, SD 10
};
// Same pins carry I2C (SSD1306) or SPI (ST7789); ST7789 adds dc/rst (bl is -1:
// the panel's LED is tied to 3V3 on this wiring, no GPIO to drive).
// Named so board_t.reserved_pins below reuses the same constants.
#define PIN_DISP_CLK  5   // ssd1306 SCL / st7789 SCLK
#define PIN_DISP_DAT  4   // ssd1306 SDA / st7789 MOSI
#define PIN_DISP_DC  20
#define PIN_DISP_RST 21
#define PIN_BTN_WAKE  0

LUGO_DISPLAY_AUTO(PIN_DISP_CLK, PIN_DISP_DAT, PIN_DISP_DC, PIN_DISP_RST, -1);
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = PIN_BTN_WAKE, .vol_up = -1, .vol_down = -1, .emotion = -1,
    .wake2 = -1,   // no second wake button on this board
};

// Display + buttons (see board_t.reserved_pins). Mic/speaker I2S pins are
// omitted on purpose — the I2S driver reserves those itself, so
// esp_gpio_is_reserved() already covers them.
LUGO_RESERVED_PINS(PIN_DISP_CLK, PIN_DISP_DAT, PIN_DISP_DC, PIN_DISP_RST,
                   PIN_BTN_WAKE);

LUGO_BOARD_REGISTER(board_lugo_c3_devkit) {
    .name        = "lugo-c3-devkit",
    .mic         = &i2s_fd_mic_ops,
    .speaker     = &i2s_fd_speaker_ops,
    .display     = NULL,                  // NULL -> auto-detect (see display_cfg)
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = &fd_cfg,               // both point at the shared full-duplex cfg
    .speaker_cfg = &fd_cfg,
    .display_cfg = &display_cfg,
    .buttons_cfg = &buttons_cfg,
    LUGO_BOARD_RESERVED_PINS,
};

#endif // CONFIG_IDF_TARGET_ESP32C3
