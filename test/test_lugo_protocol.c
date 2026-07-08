#include "lugo_protocol.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_frame_roundtrip(void) {
    uint8_t payload[3] = {0xAA, 0xBB, 0xCC};
    uint8_t buf[16];
    int n = lugo_frame_encode(LUGO_FRAME_OPUS, payload, 3, buf, sizeof buf);
    CHECK(n == 7);
    CHECK(buf[0] == LUGO_FRAME_OPUS);
    CHECK(buf[1] == 0);
    CHECK(buf[2] == 0 && buf[3] == 3);   // big-endian size
    uint8_t type; const uint8_t *p; int plen;
    CHECK(lugo_frame_decode(buf, n, &type, &p, &plen) == 0);
    CHECK(type == LUGO_FRAME_OPUS);
    CHECK(plen == 3);
    CHECK(memcmp(p, payload, 3) == 0);
}

static void test_frame_bad(void) {
    uint8_t type; const uint8_t *p; int plen;
    uint8_t two[2] = {0, 0};
    CHECK(lugo_frame_decode(two, 2, &type, &p, &plen) == -1);   // shorter than header
    uint8_t bad[6] = {0, 0, 0, 5, 1, 2};                        // says 5, has 2
    CHECK(lugo_frame_decode(bad, 6, &type, &p, &plen) == -1);
    uint8_t small[2];
    CHECK(lugo_frame_encode(LUGO_FRAME_OPUS, (const uint8_t *)"xy", 2, small, 2) == -1);  // no room
}

int main(void) {
    test_frame_roundtrip();
    test_frame_bad();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("all lugo_protocol tests passed\n");
    return 0;
}
