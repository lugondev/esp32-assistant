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
