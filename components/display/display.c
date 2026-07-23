#include "display.h"
#include "board.h"
#include <stddef.h>
#ifdef ESP_PLATFORM
#include "display_auto.h"
#include "board_i2c_probe.h"
#endif

static const display_ops_t *s_ops;

esp_err_t display_init(void) {
    const board_t *b = board_active();
    if (b->display != NULL) {          // board pins one specific panel (also the host-test path)
        s_ops = b->display;
        return s_ops->init(b->display_cfg);
    }
#ifdef ESP_PLATFORM
    // .display == NULL → auto-detect from the display_auto_cfg: probe I2C for
    // the SSD1306, else fall back to the ST7789. s_ops ends up pointing at the
    // real driver ops, so display_width()/height()/mono() below stay correct.
    const display_auto_cfg_t *ac = b->display_cfg;
    if (ac && ac->ssd1306 &&
        (board_i2c_probe(ac->ssd1306->scl, ac->ssd1306->sda, ac->ssd1306->i2c_addr, 50) ||
         board_i2c_probe(ac->ssd1306->scl, ac->ssd1306->sda, 0x3D, 50))) {
        s_ops = &display_ssd1306_ops;
        return s_ops->init(ac->ssd1306);
    }
    if (ac && ac->st7789) {
        s_ops = &display_st7789_ops;
        return s_ops->init(ac->st7789);
    }
#endif
    return ESP_FAIL;   // no panel detected → main.c runs headless
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
