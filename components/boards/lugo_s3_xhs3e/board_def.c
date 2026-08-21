#include "board_common.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3

// XH-S3E-AL V1.0 — the "AL" (audio) variant of the XH-S3E module board. Unlike
// every other board here it is not a devkit you wire up: the microphone and the
// class-D amplifier are fitted ON the board and their I2S lines never reach a
// header. What the header carries is only:
//
//     TXD  RXD  GND  VIN  IO41/SDA  IO42/SCL  3V3  GND   + a speaker pad pair
//
// So the only things the user plugs in are the SSD1306 (on the I2C pair) and a
// bare speaker (on the pads, driven by the onboard amp). Selected via
// CONFIG_AA_BOARD_LUGO_S3_XHS3E. See docs/xh-s3e-al.md.
//
// ---- Audio: CONFIRMED against the vendor's own board config ----
// The vendor ships this board as the xiaozhi-esp32 board `bread-compact-wifi-lcd`,
// whose config.h defines exactly the simplex map below (mic on I2S_NUM_0, amp on
// I2S_NUM_1). It matches the whole 41/42-display family in that tree
// (bread-compact-wifi, bread-compact-ml307, bread-compact-nt26, hu-087,
// nologo/xingzhi-cube-0.96oled), so this is settled, not inferred.
//
// One thing the vendor config settles by omission: its board .cc constructs the
// codec from these six pins and nothing else — there is NO PA-enable/shutdown
// GPIO gating the amplifier (unlike hu-087, which gates its amp on GPIO17). So
// a silent speaker here cannot be a muted amp; look at the I2S lines.
#define PIN_MIC_WS    4
#define PIN_MIC_SCK   5
#define PIN_MIC_SD    6
#define PIN_SPK_DIN   7
#define PIN_SPK_BCLK 15
#define PIN_SPK_LRC  16

// ---- Display: CONFIRMED on hardware ----
// Read off the silkscreen (IO41/SDA, IO42/SCL) and then proven at boot:
//   display: i2c device @ 0x3C (scl=42 sda=41)
//   display: display ready (ssd1306 i2c 128x64)
//
// Worth recording because the vendor config disagrees and is the wrong guide
// here: the board ships as xiaozhi's `bread-compact-wifi-lcd`, which drives an
// SPI panel on MOSI=12 / CLK=10 / DC=8 / CS=11 — pins this header does not
// expose at all. The header carries the I2C pair of the *non*-LCD
// `bread-compact-wifi` config instead. Trust the silkscreen over the config.
//
// Note for later: driving that SPI panel would need more than new pin numbers.
// display_st7789_cfg_t has no CS field and that config uses one (CS=11), so the
// ST7789 path would have to grow chip-select support first.
#define PIN_DISP_DAT 41    // SSD1306 SDA
#define PIN_DISP_CLK 42    // SSD1306 SCL

// The BOOT button, the only button this board has. It is a real GPIO once the
// bootloader has sampled it, and it is RTC-capable (GPIO0-21), so deep-sleep
// wake works. Holding it 10s still opens the setup portal.
#define PIN_BTN_WAKE  0

// Two I2S controllers, same split as lugo-s3-nx: mic on I2S_NUM_0, speaker on
// I2S_NUM_1.
static const i2s_mic_cfg_t mic_cfg = {
    .port = 0, .ws = PIN_MIC_WS, .sck = PIN_MIC_SCK, .sd = PIN_MIC_SD,
    // CONFIRMED on hardware: the loopback self-test records real varying audio
    // in the left slot (got=960/frame, peak swinging 6172..32768 with the room).
    // Which slot an onboard mic sits in is a wiring choice no vendor config
    // records, so it had to be measured. Worth keeping in mind if a revised
    // board ever reads silent: the wrong slot yields a channel of pure zeros
    // that looks exactly like a dead mic, so flip this before suspecting pins.
    .right_slot = false,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1, .bclk = PIN_SPK_BCLK, .lrc = PIN_SPK_LRC, .din = PIN_SPK_DIN,
};

// SSD1306 only — written out by hand rather than via LUGO_DISPLAY_AUTO because
// this board cannot carry the ST7789 alternative: the TFT additionally needs
// DC/RST/backlight GPIOs, and this header has no third pin to give, let alone
// three. display_auto_cfg_t takes .st7789 = NULL for exactly this case, so
// display_init() probes I2C and stops there.
static const display_ssd1306_cfg_t ssd1306_cfg = {
    .scl = PIN_DISP_CLK, .sda = PIN_DISP_DAT, .i2c_addr = 0x3C,
};
static const display_auto_cfg_t display_cfg = {
    .ssd1306 = &ssd1306_cfg, .st7789 = NULL,
};

// Wake (BOOT) is the only button in existence on this board — there is no pin
// left to put another one on. Volume therefore has no hardware control at all
// and is handled over MCP / the web UI.
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake = PIN_BTN_WAKE, .vol_up = -1, .vol_down = -1, .emotion = -1,
    .wake2 = -1,   // no second wake button on this board
};

// Display + wake button (see board_t.reserved_pins). Mic/speaker I2S pins are
// omitted on purpose — the I2S driver reserves those itself.
LUGO_RESERVED_PINS(PIN_DISP_CLK, PIN_DISP_DAT, PIN_BTN_WAKE);

LUGO_BOARD_REGISTER(board_lugo_s3_xhs3e) {
    .name        = "lugo-s3-xhs3e",
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
