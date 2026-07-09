#include "board.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static bool yes(void) { return true; }
static bool no(void)  { return false; }

static const board_t A = { .name = "a", .match = no };
static const board_t B = { .name = "b", .match = yes };
static const board_t C = { .name = "c", .match = yes };
static const board_t *const REG[] = { &A, &B, &C };
#define N ((int)(sizeof(REG)/sizeof(REG[0])))

static void test_forced_by_name(void) {
    CHECK(board_select(REG, N, "c") == &C);
    CHECK(board_select(REG, N, "a") == &A);
}
static void test_forced_missing_is_null(void) {
    CHECK(board_select(REG, N, "zzz") == NULL);
}
static void test_auto_picks_first_match(void) {
    // forced_name NULL/empty → first board whose match() is true (B, not A)
    CHECK(board_select(REG, N, NULL) == &B);
    CHECK(board_select(REG, N, "")  == &B);
}
static void test_auto_no_match_falls_back_to_first(void) {
    static const board_t X = { .name = "x", .match = no };
    static const board_t Y = { .name = "y", .match = no };
    static const board_t *const reg2[] = { &X, &Y };
    CHECK(board_select(reg2, 2, NULL) == &X);
}
static void test_empty_registry(void) {
    CHECK(board_select(REG, 0, NULL) == NULL);
    CHECK(board_select(NULL, 3, NULL) == NULL);
}
static void test_active_set_get(void) {
    CHECK(board_active() == NULL);
    board_set(&B);
    CHECK(board_active() == &B);
}

int main(void) {
    test_forced_by_name();
    test_forced_missing_is_null();
    test_auto_picks_first_match();
    test_auto_no_match_falls_back_to_first();
    test_empty_registry();
    test_active_set_get();
    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
