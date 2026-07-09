#include "board.h"
#include <string.h>

const board_t *board_select(const board_t *const *boards, int n,
                            const char *forced_name) {
    if (n <= 0 || boards == NULL) return NULL;
    if (forced_name != NULL && forced_name[0] != '\0') {
        for (int i = 0; i < n; i++)
            if (boards[i] && boards[i]->name &&
                strcmp(boards[i]->name, forced_name) == 0)
                return boards[i];
        return NULL;  // configured board not present in the build
    }
    for (int i = 0; i < n; i++)
        if (boards[i] && boards[i]->match && boards[i]->match())
            return boards[i];
    return boards[0];  // default fallback (first registered board)
}
