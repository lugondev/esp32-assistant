#include "display.h"
#include "display_ssd1306.h"
#include "display_font.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_io_i2c.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "display";

#define DISP_WIDTH      128
#define DISP_HEIGHT     64
#define DISP_PAGES      (DISP_HEIGHT / 8)

static esp_lcd_panel_handle_t s_panel;
// Set only after a fully successful init. When false (e.g. no OLED wired /
// wrong pins / wrong address), show()/flush() become no-ops instead of poking
// an uninitialised panel — the device still boots and runs headless.
static bool s_ready;

// esp_lcd's SSD1306 draw_bitmap expects color_data already packed in the
// panel's native page format (1 byte per column per 8-row page, LSB =
// topmost row in that page) — not row-major RGB565 like ST7789. We keep a
// full page-packed shadow framebuffer so flush() can set/clear individual
// bits at arbitrary (x,y) and then hand esp_lcd a page-aligned slice, since
// SSD1306's SET_PAGE_RANGE command only addresses whole 8-row pages.
static uint8_t s_fb[DISP_WIDTH * DISP_PAGES];

static void set_pixel(int x, int y, bool on) {
    if (x < 0 || x >= DISP_WIDTH || y < 0 || y >= DISP_HEIGHT) return;
    uint8_t *b = &s_fb[(y / 8) * DISP_WIDTH + x];
    uint8_t bit = 1 << (y % 8);
    if (on) *b |= bit; else *b &= ~bit;
}

// Pushes columns [x, x+w) across pages [page_start, page_end] from the
// shadow framebuffer. w*n_pages is at most DISP_WIDTH*DISP_PAGES (1KB) —
// small enough for a static scratch buffer, no heap/stack pressure.
static void push_pages(int x, int w, int page_start, int page_end) {
    if (!s_ready) return;   // no panel (init failed / none wired): run headless
    static uint8_t scratch[DISP_WIDTH * DISP_PAGES];
    int idx = 0;
    for (int p = page_start; p <= page_end; p++)
        for (int col = x; col < x + w; col++)
            scratch[idx++] = s_fb[p * DISP_WIDTH + col];
    esp_lcd_panel_draw_bitmap(s_panel, x, page_start * 8, x + w, (page_end + 1) * 8, scratch);
}

static void push_all(void) {
    push_pages(0, DISP_WIDTH, 0, DISP_PAGES - 1);
}

static void clear_screen(void) {
    memset(s_fb, 0, sizeof s_fb);
    push_all();
}

static void draw_char(int x, int y, char c) {
    const uint8_t *glyph = display_font_glyph(c);
    if (!glyph) return;
    for (int row = 0; row < DISPLAY_FONT_GLYPH_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < DISPLAY_FONT_GLYPH_WIDTH; col++)
            set_pixel(x + col, y + row, (bits >> col) & 1);
    }
}

static void draw_line(const char *text, int y) {
    int x = display_layout_line(text, DISP_WIDTH);
    if (x < 0) return;  // too wide for the screen — skip rather than wrap
    for (const char *p = text; *p; p++) {
        draw_char(x, y, *p);
        x += DISPLAY_FONT_GLYPH_WIDTH;
    }
}

static esp_err_t ssd1306_init(const void *cfg_v) {
    const display_ssd1306_cfg_t *c = (const display_ssd1306_cfg_t *)cfg_v;
#if CONFIG_AA_SKIP_DISPLAY_INIT
    // Bring-up isolation: no I2C traffic at all; s_ready stays false so
    // show()/flush() are no-ops. See CONFIG_AA_SKIP_DISPLAY_INIT.
    (void)c;
    ESP_LOGW(TAG, "display init skipped (CONFIG_AA_SKIP_DISPLAY_INIT)");
    return ESP_OK;
#else

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = c->sda,
        .scl_io_num = c->scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    // Bring-up aid: log every address that ACKs on this bus, so a silent OLED
    // (wrong pins / SDA-SCL swap / unpowered / wrong 0x3C vs 0x3D) is instantly
    // diagnosable from the boot log instead of just aborting on a NACK.
    int found = 0;
    for (uint16_t a = 0x08; a <= 0x77; a++) {
        if (i2c_master_probe(bus_handle, a, 50) == ESP_OK) {
            ESP_LOGI(TAG, "i2c device @ 0x%02X (scl=%d sda=%d)", a, c->scl, c->sda);
            found++;
        }
    }
    if (!found) ESP_LOGW(TAG, "i2c scan found NOTHING on scl=%d/sda=%d — check wiring/power", c->scl, c->sda);

    // From here on, failures are non-fatal: log and return an error so the
    // device boots headless (see s_ready) rather than boot-looping on abort().
#define DISP_TRY(expr) do { esp_err_t _e = (expr); if (_e != ESP_OK) { \
        ESP_LOGE(TAG, "display init: %s failed (%s) — running headless", #expr, esp_err_to_name(_e)); \
        return _e; } } while (0)

    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = c->i2c_addr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,   // SSD1306's I2C control-byte D/C bit position
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        // 100kHz (not 400): tolerant of weak/absent external pull-ups and long
        // dupont wiring during bring-up. A status/eyes panel doesn't need 400.
        .scl_speed_hz = 100000,
    };
    DISP_TRY(esp_lcd_new_panel_io_i2c(bus_handle, &io_config, &io_handle));

    esp_lcd_panel_ssd1306_config_t vendor_cfg = { .height = DISP_HEIGHT };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,   // no RST pin on a 4-pin (VCC/GND/SCL/SDA) module
        .bits_per_pixel = 1,
        .vendor_config = &vendor_cfg,
    };
    DISP_TRY(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &s_panel));

    DISP_TRY(esp_lcd_panel_reset(s_panel));   // no-op (reset_gpio_num == -1)
    DISP_TRY(esp_lcd_panel_init(s_panel));
    // This module's default orientation is rotated 180 (both axes reversed,
    // not just upside-down) — mirroring only Y fixed vertical but left
    // horizontal reversed, showing as a left-right mirror. Flip both.
    DISP_TRY(esp_lcd_panel_mirror(s_panel, true, true));
    DISP_TRY(esp_lcd_panel_disp_on_off(s_panel, true));
#undef DISP_TRY

    s_ready = true;
    clear_screen();
    ESP_LOGI(TAG, "display ready (ssd1306 i2c %dx%d)", DISP_WIDTH, DISP_HEIGHT);
    return ESP_OK;
#endif // CONFIG_AA_SKIP_DISPLAY_INIT
}

static void ssd1306_show(const char *line1, const char *line2) {
    memset(s_fb, 0, sizeof s_fb);
    if (line2 == NULL) {
        draw_line(line1, (DISP_HEIGHT - DISPLAY_FONT_GLYPH_HEIGHT) / 2);
    } else {
        int gap = 4;
        int total_h = 2 * DISPLAY_FONT_GLYPH_HEIGHT + gap;
        int y1 = (DISP_HEIGHT - total_h) / 2;
        int y2 = y1 + DISPLAY_FONT_GLYPH_HEIGHT + gap;
        draw_line(line1, y1);
        draw_line(line2, y2);
    }
    push_all();
}

// Thresholds by luminance, not by "is it exactly zero": the app's chosen
// background color (main.c's HUD_BG, a dark navy) is non-zero on a color
// panel but is clearly meant to read as off/black — a plain "!= 0" test
// would light up every background pixel, not just the shapes drawn on top.
// 20 sits just above HUD_BG's luma (~18) and below every other color this
// app actually draws (pure blue is the closest at ~28, from the boot color
// bars) — a bigger palette change may need revisiting this. This is also
// the point where gradient fidelity (e.g. robot_eyes' glow halo) collapses
// to a hard on/off edge on real monochrome hardware.
static bool pixel_on(uint16_t rgb565) {
    int r8 = ((rgb565 >> 11) & 0x1F) * 255 / 31;
    int g8 = ((rgb565 >> 5) & 0x3F) * 255 / 63;
    int b8 = (rgb565 & 0x1F) * 255 / 31;
    int luma = (r8 * 30 + g8 * 59 + b8 * 11) / 100;
    return luma > 20;
}

// Not host-tested, same as st7789_flush: exercises real I2C hardware only.
static void ssd1306_flush(int x, int y, int w, int h, const uint16_t *rgb565) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            set_pixel(x + col, y + row, pixel_on(rgb565[row * w + col]));

    int page_start = y / 8;
    int page_end = (y + h - 1) / 8;
    if (page_end >= DISP_PAGES) page_end = DISP_PAGES - 1;
    push_pages(x, w, page_start, page_end);
}

// No backlight pin on a self-emitting OLED — accepted for interface
// compatibility (mcp_tools' self.screen.set_backlight stays safe to call,
// it just has no visible effect on this board).
static void ssd1306_set_backlight(bool on) { (void)on; }

const display_ops_t display_ssd1306_ops = {
    .init = ssd1306_init,
    .show = ssd1306_show,
    .flush = ssd1306_flush,
    .width = DISP_WIDTH,
    .height = DISP_HEIGHT,
    .mono = true,   // 1-bit panel: ssd1306_flush thresholds via pixel_on()
    .set_backlight = ssd1306_set_backlight,
};
