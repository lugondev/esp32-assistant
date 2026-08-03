#include "board.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const board_t A = { .name = "a" };
static const board_t B = { .name = "b" };
static const board_t C = { .name = "c" };
static const board_t *const REG[] = { &A, &B, &C };
#define N ((int)(sizeof(REG)/sizeof(REG[0])))

static void test_selects_by_name(void) {
    CHECK(board_select(REG, N, "c") == &C);
    CHECK(board_select(REG, N, "a") == &A);
}
static void test_unknown_name_is_null(void) {
    CHECK(board_select(REG, N, "zzz") == NULL);
}
// The old implementation fell back to boards[0] whenever no name matched, so a
// typo in CONFIG_AA_BOARD_NAME booted a real board with the wrong pinout and
// logged nothing. NULL is the point of this change: the caller turns it into
// ESP_ERR_NOT_FOUND and a log naming the board it could not find.
static void test_no_name_is_null_not_first(void) {
    CHECK(board_select(REG, N, NULL) == NULL);
    CHECK(board_select(REG, N, "")   == NULL);
}
static void test_empty_registry(void) {
    CHECK(board_select(REG, 0, "a")  == NULL);
    CHECK(board_select(NULL, 3, "a") == NULL);
}
static void test_active_set_get(void) {
    CHECK(board_active() == NULL);
    board_set(&B);
    CHECK(board_active() == &B);
}

int main(void) {
    test_selects_by_name();
    test_unknown_name_is_null();
    test_no_name_is_null_not_first();
    test_empty_registry();
    test_active_set_get();
    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
