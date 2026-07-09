#include "board.h"

static const board_t *s_active;

const board_t *board_active(void) { return s_active; }
void           board_set(const board_t *b) { s_active = b; }
