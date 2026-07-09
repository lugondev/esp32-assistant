#include "buttons.h"
#include "board.h"

void buttons_start(void (*on_press)(button_id_t id)) {
    board_active()->buttons->start(on_press);
}
