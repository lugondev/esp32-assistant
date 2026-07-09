#include "display.h"
#include "board.h"

static const display_ops_t *s_ops;

esp_err_t display_init(void) {
    s_ops = board_active()->display;
    return s_ops->init(board_active()->display_cfg);
}
void display_show(const char *line1, const char *line2) {
    s_ops->show(line1, line2);
}
void display_flush(int x, int y, int w, int h, const uint16_t *rgb565) {
    s_ops->flush(x, y, w, h, rgb565);
}
int display_width(void)  { return s_ops->width; }
int display_height(void) { return s_ops->height; }
void display_set_backlight(bool on) { s_ops->set_backlight(on); }
