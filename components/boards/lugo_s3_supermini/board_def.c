#include "board_common.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3

// ESP32-S3 SuperMini: a compact S3 board (ESP32-S3FH4R2 — 4MB in-package flash,
// 2MB QUAD PSRAM), carrying the same peripherals as lugo-s3-nx (MAX98357A +
// INMP441 on dual I2S, SSD1306 OLED) but on its own, much tighter pinout.
// Selected via CONFIG_AA_BOARD_LUGO_S3_SUPERMINI. See docs/s3-supermini.md.
//
// SuperMini pin notes: only TX/RX + GP1-GP13 are broken out. GPIO0/45/46 (the
// S3 strapping pins that matter) are NOT on the header, so nothing here can
// disturb boot mode. GPIO19/20 are the native USB pins the board flashes and
// consoles over — never route a peripheral to them.
// GPIO4 IS UNUSABLE ON THIS BOARD — it is shorted to ground. Driving it high
// from the ESP32's own push-pull output still reads back 0, and an internal
// pull-up cannot lift it, with nothing connected to the header. Every other
// broken-out pin (2/3/5/8/9/10/11/13/43) drives both ways cleanly. Whatever
// lands on GPIO4 reads as a permanent logic 0: as mic SD that was a dead data
// line, as mic WS the mic never got a word-select and emitted nothing.
#define PIN_MIC_WS    7    // INMP441 WS   (was 4 — dead pin)
#define PIN_MIC_SCK   6    // INMP441 SCK  (I2S BCLK)
#define PIN_MIC_SD    5    // INMP441 SD   (I2S data-in). Health-scan readings
                           // on this line have flip-flopped between GPIO5 and
                           // GPIO10 (OK on one boot, DEAD/STUCK the next) —
                           // points at a marginal solder joint on the SD wire
                           // itself, not either ESP32 pin. Back on GPIO5 while
                           // re-testing.

#define PIN_SPK_LRC   1    // MAX98357A LRC (I2S WS)
#define PIN_SPK_BCLK 44    // MAX98357A BCLK — the header pin silkscreened "RX"
                           // (UART0 RX). Was recorded as GPIO43 (TX) from an
                           // earlier verbal description, but a continuity
                           // check with a multimeter found BCLK actually
                           // landed on the RX-labeled pin — a one-pin-over
                           // mixup that meant the amp never saw a bit clock
                           // at all despite VIN/SD/GAIN/GND all measuring
                           // correct. Safe because the console runs on the
                           // native USB-Serial-JTAG, not UART0.
#define PIN_SPK_DIN   2    // MAX98357A DIN (I2S data-out)

#define PIN_DISP_CLK 12    // SSD1306 SCL
#define PIN_DISP_DAT 11    // SSD1306 SDA
#define PIN_BTN_WAKE  3    // RTC-capable (GPIO0-21), so deep-sleep wake works.
                           // Moved here from GPIO13, which an isolated test
                           // (wire fully disconnected) found stuck at 0 —
                           // unusable, and worse than useless for an
                           // active-low button: a pin that reads 0 forever is
                           // indistinguishable from a permanently held press,
                           // so the driver would fire BTN_WAKE at boot and
                           // BTN_WAKE_HOLD (setup portal) 10s later, every
                           // boot. GPIO3 drive-tested healthy; the earlier
                           // "GPIO3 is dead" verdict was a false positive from
                           // leaving the wake wire attached during that test.
                           // GPIO3 is an S3 strapping pin (JTAG source select)
                           // but is safe as an active-low input: the internal
                           // pull-up holds it high through reset, and it only
                           // matters if driven at boot.
#define PIN_BTN_BOOT  0    // On-board BOOT button, wired to GPIO0 on the module
                           // itself (not on the header). Doubles as a second
                           // wake button — same press/hold behaviour — so the
                           // device stays usable without the soldered wake
                           // wire. GPIO0 is the S3's boot-mode strapping pin:
                           // holding it through reset enters the download
                           // bootloader instead of the app, which is the
                           // button's normal job and is unaffected by polling
                           // it as an input afterwards.

// GPIO4 is deliberately absent from every assignment above. Do not "reclaim" it
// as a free pin: on the unit this board was brought up on it is shorted to
// ground, and a pin held at logic 0 fails silently rather than loudly. To
// re-check on another unit, drive it high and read it back — a healthy pin
// returns 1, this one returns 0. See docs/s3-supermini.md.

// Two I2S controllers, same split as lugo-s3-nx: mic on I2S_NUM_0, speaker on
// I2S_NUM_1. No clock fan-out needed (that is the single-I2S C3's problem).
static const i2s_mic_cfg_t mic_cfg = {
    .port = 0, .ws = PIN_MIC_WS, .sck = PIN_MIC_SCK, .sd = PIN_MIC_SD,
    // Originally documented as L/R tied HIGH (right slot) on this unit's first
    // bring-up. Re-checked during a later debug session: L/R is actually wired
    // to GND (confirmed by the person holding the board), which drives the
    // LEFT slot instead — matching every other board here. mic meter read a
    // flat peak=0 the whole time .right_slot was true, exactly the symptom of
    // reading the silent slot while real audio sits in the other one.
    .right_slot = false,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1, .bclk = PIN_SPK_BCLK, .lrc = PIN_SPK_LRC, .din = PIN_SPK_DIN,
};

// SSD1306 only — written out by hand rather than via LUGO_DISPLAY_AUTO because
// this board cannot carry the ST7789 alternative: the TFT additionally needs
// DC/RST (and the SuperMini header has too few pins left to commit two of them
// to a panel that is not fitted). display_auto_cfg_t takes .st7789 = NULL for
// exactly this case, so display_init() probes I2C and stops there.
static const display_ssd1306_cfg_t ssd1306_cfg = {
    .scl = PIN_DISP_CLK, .sda = PIN_DISP_DAT, .i2c_addr = 0x3C,
};
static const display_auto_cfg_t display_cfg = {
    .ssd1306 = &ssd1306_cfg, .st7789 = NULL,
};

// Only wake buttons are wired; volume is handled over MCP/the web UI. Two of
// them: the soldered wake wire and the module's own BOOT button, both firing
// BTN_WAKE and both hold-to-portal capable.
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = PIN_BTN_WAKE, .vol_up = -1, .vol_down = -1, .emotion = -1,
    .wake2 = PIN_BTN_BOOT,
};

// Display + wake button (see board_t.reserved_pins). Mic/speaker I2S pins are
// omitted on purpose — the I2S driver reserves those itself.
LUGO_RESERVED_PINS(PIN_DISP_CLK, PIN_DISP_DAT, PIN_BTN_WAKE, PIN_BTN_BOOT);

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
};

#endif // CONFIG_IDF_TARGET_ESP32S3
