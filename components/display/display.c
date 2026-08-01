#include "display.h"
#include "board.h"
#include <stddef.h>
#ifdef ESP_PLATFORM
#include "display_auto.h"
#include "board_i2c_probe.h"
#include "esp_log.h"

static const char *TAG = "display";

// An I2C bus with nothing driving it reads as ACK for EVERY address: SDA stuck
// low is indistinguishable from an ack bit. Observed on a lugo-s3-nx with the
// ST7789 soldered -- all 112 addresses "answered", so the 0x3C probe below
// false-positived, the device committed to the SSD1306, its first real
// transaction failed, and the screen stayed blank. A sentinel address that no
// panel uses tells the two apart: if that answers too, the bus is lying.
#define I2C_STUCK_BUS_SENTINEL 0x08
#endif

// Headless fallback, so s_ops is NEVER NULL. display_init() can legitimately
// fail (no panel wired, a detected panel that won't initialise, a board with
// no display option at all) and main.c is written to carry on in that case —
// but every accessor below dereferences s_ops unconditionally, so a NULL left
// behind by a failed init turned "continue headless" into a crash on the very
// next display_show(). Pointing at no-ops instead makes the documented
// contract true.
//
// width/height are 0 on purpose: that is the "no pixel panel" signal callers
// already test (see board_types.h's display_ops_t note and main.c's
// status_task), so the HUD path skips its buffer allocations and rendering
// rather than drawing into a driver that isn't there.
static void headless_show(const char *line1, const char *line2) { (void)line1; (void)line2; }
static void headless_flush(int x, int y, int w, int h, const uint16_t *rgb565) {
    (void)x; (void)y; (void)w; (void)h; (void)rgb565;
}
static void headless_set_backlight(bool on) { (void)on; }
static const display_ops_t s_headless_ops = {
    .init = NULL, .show = headless_show, .flush = headless_flush,
    .width = 0, .height = 0, .mono = false, .set_backlight = headless_set_backlight,
};

static const display_ops_t *s_ops = &s_headless_ops;

esp_err_t display_init(void) {
    const board_t *b = board_active();
    if (b->display != NULL) {          // board pins one specific panel (also the host-test path)
        s_ops = b->display;
        esp_err_t err = s_ops->init(b->display_cfg);
        // Fall back rather than keep a driver that failed to come up: its
        // width/height would still advertise a panel that can't be drawn on,
        // and callers gate on width>0 to decide whether to allocate render
        // buffers at all.
        if (err != ESP_OK) s_ops = &s_headless_ops;
        return err;
    }
#ifdef ESP_PLATFORM
    // .display == NULL → auto-detect from the display_auto_cfg: probe I2C for
    // the SSD1306, else fall back to the ST7789. s_ops ends up pointing at the
    // real driver ops, so display_width()/height()/mono() below stay correct.
    const display_auto_cfg_t *ac = b->display_cfg;
    if (ac && ac->ssd1306) {
        const int scl = ac->ssd1306->scl, sda = ac->ssd1306->sda;
        bool detected = false;
        if (board_i2c_probe(scl, sda, I2C_STUCK_BUS_SENTINEL, 50)) {
            // Every address answers -- see I2C_STUCK_BUS_SENTINEL. Believing the
            // probe here is what left an ST7789 board headless.
            ESP_LOGW(TAG, "i2c scl=%d/sda=%d acks even 0x%02X — no real device, skipping ssd1306",
                     scl, sda, I2C_STUCK_BUS_SENTINEL);
        } else {
            detected = board_i2c_probe(scl, sda, ac->ssd1306->i2c_addr, 50) ||
                       board_i2c_probe(scl, sda, 0x3D, 50);
        }
        if (detected) {
            s_ops = &display_ssd1306_ops;
            esp_err_t err = s_ops->init(ac->ssd1306);
            if (err == ESP_OK) return ESP_OK;
            // Detected but unusable (unpowered mid-boot, flaky wiring, an
            // address that answers but isn't an SSD1306). The board contract is
            // "SSD1306 if one answers, otherwise the ST7789", so fall through
            // instead of going headless with a panel still untried.
            ESP_LOGW(TAG, "ssd1306 detected but init failed (%s) — trying the st7789",
                     esp_err_to_name(err));
        }
    }
    if (ac && ac->st7789) {
        s_ops = &display_st7789_ops;
        esp_err_t err = s_ops->init(ac->st7789);
        if (err != ESP_OK) s_ops = &s_headless_ops;   // last option exhausted
        return err;
    }
#endif
    // Nothing worked. Reaching here does NOT imply s_ops is untouched: the
    // ssd1306 branch above assigns it before trying to init, and falls through
    // on failure, so a board that offers an SSD1306 but no ST7789 arrives here
    // still pointing at the failed driver. Its show/flush are inert (that
    // driver has its own s_ready guard), but it would keep advertising a
    // 128x64 panel, and width>0 is exactly what makes main.c's status_task
    // allocate HUD buffers and render frames nobody can see. Reset explicitly.
    s_ops = &s_headless_ops;
    return ESP_FAIL;
}
void display_show(const char *line1, const char *line2) {
    s_ops->show(line1, line2);
}
void display_flush(int x, int y, int w, int h, const uint16_t *rgb565) {
    s_ops->flush(x, y, w, h, rgb565);
}
int display_width(void)  { return s_ops->width; }
int display_height(void) { return s_ops->height; }
bool display_is_mono(void) { return s_ops->mono; }
void display_set_backlight(bool on) { s_ops->set_backlight(on); }
