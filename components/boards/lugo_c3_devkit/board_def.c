#include "board.h"
#include "i2s_fd.h"
#include "display_auto.h"
#include "buttons_gpio.h"
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
// Same pins carry I2C (SSD1306) or SPI (ST7789); ST7789 adds dc/rst/bl.
static const display_ssd1306_cfg_t ssd1306_cfg = { .scl = 5, .sda = 4, .i2c_addr = 0x3C };
static const display_st7789_cfg_t  st7789_cfg  = { .sclk = 5, .mosi = 4, .dc = 20, .rst = 21, .bl = -1 };
static const display_auto_cfg_t    display_cfg = { .ssd1306 = &ssd1306_cfg, .st7789 = &st7789_cfg };
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = 0, .vol_up = -1, .vol_down = -1, .emotion = -1,
};

static bool match(void) { return true; }  // Kconfig-forced; the active C3 board

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
    .match       = match,
};

#endif // CONFIG_IDF_TARGET_ESP32C3
