#include "board.h"
#include <string.h>

// Name lookup only. Auto-detection was removed 2026-08-03: every board's
// match() was a compile-time constant, so the "first board whose match() is
// true" path only ever returned whichever such board linked first. An unmatched
// name returns NULL rather than boards[0] — a wrong name must fail loudly, not
// boot a real board with someone else's pinout.
const board_t *board_select(const board_t *const *boards, int n,
                            const char *name) {
    if (n <= 0 || boards == NULL) return NULL;
    if (name == NULL || name[0] == '\0') return NULL;
    for (int i = 0; i < n; i++)
        if (boards[i] && boards[i]->name &&
            strcmp(boards[i]->name, name) == 0)
            return boards[i];
    return NULL;
}
