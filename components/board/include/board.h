#pragma once
#include "board_types.h"

// The board selected at boot. NULL until board_detect_and_select() succeeds.
const board_t *board_active(void);
// Force the active board. Boot path uses it via board_detect_and_select();
// host tests use it directly to install a mock board.
void           board_set(const board_t *b);
// Boot entry: gather registered boards, pick one (forced or auto-detected),
// and board_set() it. Defined in Task 5 (target-only).
esp_err_t      board_detect_and_select(void);

// Pure selection: forced_name (non-empty) picks by name; otherwise the first
// board whose match() returns true; otherwise boards[0]. NULL if forced name
// not found or n<=0.
const board_t *board_select(const board_t *const *boards, int n,
                            const char *forced_name);

// Define a board and auto-register it into the linker "board_desc" section:
//   LUGO_BOARD_REGISTER(board_my_name) { .name = "my-name", ... };
#define LUGO_BOARD_REGISTER(sym)                                          \
    static const board_t sym;                                             \
    static const board_t *const sym##_ref                                 \
        __attribute__((used, section("board_desc"))) = &sym;              \
    static const board_t sym =
