#pragma once
#include "board_types.h"

// The board selected at boot. NULL until board_select_configured() succeeds.
const board_t *board_active(void);
// Force the active board. Boot path uses it via board_select_configured();
// host tests use it directly to install a mock board.
void           board_set(const board_t *b);
// Boot entry: look up CONFIG_AA_BOARD_NAME among the registered boards and
// board_set() it. Target-only.
esp_err_t      board_select_configured(void);

// Pure lookup by name. NULL if the name is NULL/empty, no registered board
// carries it, or n<=0. There is deliberately no fallback: see board_select.c.
const board_t *board_select(const board_t *const *boards, int n,
                            const char *name);

// Define a board and auto-register it into the linker "board_desc" section:
//   LUGO_BOARD_REGISTER(board_my_name) { .name = "my-name", ... };
#define LUGO_BOARD_REGISTER(sym)                                          \
    static const board_t sym;                                             \
    static const board_t *const sym##_ref                                 \
        __attribute__((used, section("board_desc"))) = &sym;              \
    static const board_t sym =
