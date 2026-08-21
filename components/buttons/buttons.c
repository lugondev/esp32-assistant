#include "buttons.h"
#include "board.h"

void buttons_start(void (*on_press)(button_id_t id)) {
    board_active()->buttons->start(on_press);
}

void buttons_set_confirm_mode(bool on) {
    const buttons_ops_t *ops = board_active()->buttons;
    if (ops && ops->set_confirm_mode) ops->set_confirm_mode(on);
}
