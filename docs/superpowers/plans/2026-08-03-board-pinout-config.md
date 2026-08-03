# Board Pinout Config Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `lugo-custom` board whose pins come from Kconfig, delete the board auto-detection that never detected anything, and stop two boards sharing one global set of audio pins.

**Architecture:** Hand-written `board_def.c` files stay the home for known boards, because half of what they contain is hardware reasoning that Kconfig cannot carry. One new board, `lugo-custom`, reads every pin from `CONFIG_AA_CUSTOM_*` so new hardware needs no new files. With the custom board covering the "pins not known yet" case, `match()` — all five of which are compile-time constants — is deleted, and board selection becomes a plain name lookup.

**Tech Stack:** ESP-IDF v5.x, Kconfig, CMake, C11. Host tests are plain C compiled by `test/Makefile` with `cc`.

**Spec:** `docs/superpowers/specs/2026-08-03-board-pinout-config-design.md`

## Global Constraints

- **A pin value must never change silently.** Task 2 moves the NX's audio pins between files; the resulting bytes must be proven identical. A refactor that quietly relocates the NX's mic is indistinguishable from the silent-mic failure this tree already spent a day on.
- **Every `LUGO_RESERVED_PINS(...)` argument must be the same named constant or `CONFIG_` symbol the cfg struct uses.** Never a re-typed literal. A hand-written second copy is what let the reserved list go stale against a board's real pinout once already.
- **Existing boards keep their comments verbatim.** The GPIO4 / GPIO44 / `right_slot` notes in `lugo_s3_supermini/board_def.c` are measurement records, not commentary. Tasks here delete `match()` lines only.
- **Host tests must pass after every task:** `make -C test test` — 13 binaries plus the MCP frame-size check.
- **`right_slot` stays hand-declared.** No probe, no autodetect. Decided in the spec; do not add one.
- Target boards and their names: `lugo-s3-nx`, `lugo-s3-supermini`, `lugo-s3-wroom` (esp32s3); `lugo-c3-devkit`, `lugo-c3-supermini` (esp32c3); `lugo-custom` (both).

---

### Task 1: Delete auto-detection

`match()` never selected anything — `lugo_s3_nx` and `lugo_c3_devkit` return a literal `true`, the other three a literal `false`. `board_select()`'s auto path therefore picks "first board linked with a hardcoded true", and its `return boards[0]` fallback means a typo'd board name boots a wrong pinout instead of failing.

**Files:**
- Modify: `test/test_board_select.c` (rewrite fixtures and cases)
- Modify: `components/board/board_select.c:1-18`
- Modify: `components/board/include/board.h:8-18`
- Modify: `components/board/include/board_types.h:133` (drop `match` member)
- Modify: `components/board/board.c:11-27`
- Modify: `main/main.c:1241` (renamed call)
- Modify: `components/audio/audio.c:6`, `components/mcp_tools/registry.c:14` (comment references)
- Modify: `main/Kconfig.projbuild:105-144`
- Modify: `components/boards/lugo_s3_nx/board_def.c:51,64`
- Modify: `components/boards/lugo_s3_supermini/board_def.c:80,93`
- Modify: `components/boards/lugo_s3_wroom/board_def.c:48,61`
- Modify: `components/boards/lugo_c3_devkit/board_def.c:42,55`
- Modify: `components/boards/lugo_c3_supermini/board_def.c:36,49`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `const board_t *board_select(const board_t *const *boards, int n, const char *name)` — name-only lookup, returns `NULL` when `name` is `NULL`/empty/unmatched or `n <= 0` or `boards == NULL`. `esp_err_t board_select_configured(void)` replaces `board_detect_and_select()`. `board_t` no longer has a `match` member.

- [ ] **Step 1: Rewrite the failing test**

Replace the whole of `test/test_board_select.c` with:

```c
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make -C test test_board_select && ./test/test_board_select`
Expected: FAIL — `test_no_name_is_null_not_first` reports two failures, because the current implementation returns `&B` (first `match()`-true) and `&A` (fallback) rather than `NULL`.

- [ ] **Step 3: Rewrite `board_select()`**

Replace the whole of `components/board/board_select.c` with:

```c
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
```

- [ ] **Step 4: Drop the `match` member from `board_t`**

In `components/board/include/board_types.h`, delete this line from the `board_t` struct (it is the last member, immediately after `n_reserved_pins`):

```c
    bool               (*match)(void); // true if firmware is running on this board
```

- [ ] **Step 5: Update the declarations in `board.h`**

In `components/board/include/board.h`, replace the three comment/declaration blocks:

```c
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
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `make -C test test_board_select && ./test/test_board_select`
Expected: `OK`

- [ ] **Step 7: Update the boot path**

Replace `board_detect_and_select()` in `components/board/board.c` (keep the `extern` boundary-symbol declarations and `TAG` above it):

```c
esp_err_t board_select_configured(void) {
    int n = (int)(_board_desc_end - _board_desc_start);
    const board_t *b = board_select(_board_desc_start, n, CONFIG_AA_BOARD_NAME);
    if (b == NULL) {
        ESP_LOGE(TAG, "no board named \"%s\" among %d registered",
                 CONFIG_AA_BOARD_NAME, n);
        return ESP_ERR_NOT_FOUND;
    }
    board_set(b);
    ESP_LOGI(TAG, "board: %s (registered=%d)", b->name, n);
    return ESP_OK;
}
```

The `#ifdef CONFIG_AA_BOARD_FORCE` / `#else` pair around `forced` goes away with it — `CONFIG_AA_BOARD_NAME` is now always set by Kconfig.

- [ ] **Step 8: Update the call site and the two comment references**

`main/main.c:1241` — change `ESP_ERROR_CHECK(board_detect_and_select());` to:

```c
    ESP_ERROR_CHECK(board_select_configured());
```

`components/audio/audio.c:6` — change the comment to read `board_select_configured() must run (app_main) before audio_init().`

`components/mcp_tools/registry.c:14` — change `for symmetry with board_detect_and_select()` to `for symmetry with board_select_configured()`.

- [ ] **Step 9: Delete `match()` from all five board files**

In each of the five `board_def.c` files, delete the `static bool match(void)` line and the `.match       = match,` line from the `LUGO_BOARD_REGISTER` body. The exact `match()` line differs per file — delete whichever is present:

```c
static bool match(void) { return true; }   // lugo_s3_nx, lugo_c3_devkit
static bool match(void) { return false; }  // lugo_s3_supermini, lugo_s3_wroom, lugo_c3_supermini
```

Their trailing comments (`// the S3 autodetect default`, `// opt-in via CONFIG_AA_BOARD_LUGO_S3_SUPERMINI`, …) go with them. Where a file's header comment also mentions match(), update it — in `lugo_s3_supermini/board_def.c:11-12` replace:

```c
// Selected only via CONFIG_AA_BOARD_LUGO_S3_SUPERMINI (match() returns false),
// so the NX stays the S3 autodetect default. See docs/s3-supermini.md.
```

with:

```c
// Selected via CONFIG_AA_BOARD_LUGO_S3_SUPERMINI. See docs/s3-supermini.md.
```

Apply the equivalent one-line trim to `lugo_c3_supermini/board_def.c:9-10` and `lugo_s3_wroom/board_def.c:9-10`, which carry the same "(match() returns false)" phrasing.

- [ ] **Step 10: Update Kconfig**

In `main/Kconfig.projbuild`, replace lines 105-144 (the `choice AA_BOARD` block through the end of `AA_BOARD_NAME`) with:

```
choice AA_BOARD
    prompt "Target board"
    default AA_BOARD_LUGO_S3_NX if IDF_TARGET_ESP32S3
    default AA_BOARD_LUGO_C3_DEVKIT if IDF_TARGET_ESP32C3

config AA_BOARD_LUGO_S3_NX
    bool "Lugo S3 NX — N-series modules N16R8/N8R8 (auto SSD1306/ST7789 + MAX98357A/INMP441 dual-I2S)"
    depends on IDF_TARGET_ESP32S3

config AA_BOARD_LUGO_S3_SUPERMINI
    bool "Lugo S3 SuperMini — ESP32-S3FH4R2, 4MB flash + 2MB quad PSRAM (SSD1306 I2C + MAX98357A/INMP441 dual-I2S)"
    depends on IDF_TARGET_ESP32S3

config AA_BOARD_LUGO_S3_WROOM
    bool "Lugo S3 WROOM (auto display, camera-capable) [placeholder pins]"
    depends on IDF_TARGET_ESP32S3

config AA_BOARD_LUGO_C3_DEVKIT
    bool "Lugo C3 DevKitM-1 (SSD1306 0.96in I2C + MAX98357A/INMP441 full-duplex I2S)"
    depends on IDF_TARGET_ESP32C3

config AA_BOARD_LUGO_C3_SUPERMINI
    bool "Lugo C3 SuperMini (SSD1306 0.96in I2C + MAX98357A/INMP441 full-duplex I2S)"
    depends on IDF_TARGET_ESP32C3
endchoice

config AA_BOARD_NAME
    string
    default "lugo-s3-nx" if AA_BOARD_LUGO_S3_NX
    default "lugo-s3-supermini" if AA_BOARD_LUGO_S3_SUPERMINI
    default "lugo-s3-wroom" if AA_BOARD_LUGO_S3_WROOM
    default "lugo-c3-devkit" if AA_BOARD_LUGO_C3_DEVKIT
    default "lugo-c3-supermini" if AA_BOARD_LUGO_C3_SUPERMINI
    default ""
```

`AA_BOARD_AUTODETECT` and `AA_BOARD_FORCE` are gone. `default ""` stays as an unreachable safety net — `board_select_configured()` turns it into a clear error rather than a wrong board.

Task 3 adds `AA_BOARD_CUSTOM` to this choice; leave room for it.

- [ ] **Step 11: Verify both targets still build**

Run:
```bash
rm -rf build-verify && \
idf.py -B build-verify -DSDKCONFIG=build-verify/sdkconfig set-target esp32s3 && \
idf.py -B build-verify -DSDKCONFIG=build-verify/sdkconfig build
```
Expected: build succeeds; the boot log line `board: lugo-s3-nx (registered=3)` is produced by the code just written (not verifiable without hardware — confirm the string compiles into `board.c`).

Then the same for the C3:
```bash
rm -rf build-verify-c3 && \
idf.py -B build-verify-c3 -DSDKCONFIG=build-verify-c3/sdkconfig set-target esp32c3 && \
idf.py -B build-verify-c3 -DSDKCONFIG=build-verify-c3/sdkconfig build
```
Expected: build succeeds.

The pre-existing `sdkconfig`, `sdkconfig.s3sm`, and `sdkconfig.c3` still contain `CONFIG_AA_BOARD_AUTODETECT`/`CONFIG_AA_BOARD_FORCE` lines. All three already select a real board explicitly (checked: `lugo-c3-devkit`, `lugo-s3-supermini`), so the stale lines are dropped on the next regeneration and change no behaviour. Leave them; do not hand-edit generated sdkconfigs.

- [ ] **Step 12: Run the full host test suite**

Run: `make -C test test`
Expected: all 13 binaries print `OK`, then the MCP frame-size check passes.

- [ ] **Step 13: Commit**

```bash
git add components/board components/boards components/audio/audio.c \
        components/mcp_tools/registry.c main/main.c main/Kconfig.projbuild \
        test/test_board_select.c
git commit -m "refactor(board): delete auto-detection that never detected

All five match() were compile-time constants, so the auto path only ever
returned whichever hardcoded-true board linked first, and the boards[0]
fallback meant a typo'd CONFIG_AA_BOARD_NAME booted a real board with the
wrong pinout. board_select() is now a name lookup that returns NULL, and
board_detect_and_select() is board_select_configured() because it no
longer detects anything."
```

---

### Task 2: Reclaim the global audio pin symbols

`AA_MIC_WS/SCK/SD` and `AA_SPK_BCLK/LRC/DIN` are global Kconfig symbols read by both `lugo-s3-nx` and `lugo-s3-wroom`, so those two boards cannot be given different audio pins — changing one to suit the other silently breaks the other. Nothing overrides the defaults today (checked: every `sdkconfig*` in the repo carries `4/5/6` and `15/16/7`), so the values move into the two board files as literals and the symbols are deleted. Task 3 reintroduces them under `AA_CUSTOM_*`, scoped to the one board where a single global value is correct.

**Files:**
- Modify: `components/boards/lugo_s3_nx/board_def.c:14-19`
- Modify: `components/boards/lugo_s3_wroom/board_def.c:17-22`
- Modify: `main/Kconfig.projbuild:36-54` (delete six configs)

**Interfaces:**
- Consumes: Task 1's `board_t` without `match`.
- Produces: `CONFIG_AA_MIC_*` and `CONFIG_AA_SPK_*` no longer exist. `PIN_MIC_WS/SCK/SD` and `PIN_SPK_BCLK/LRC/DIN` are file-local `#define`s in the NX and WROOM board files.

- [ ] **Step 1: Record the baseline bytes**

This must happen before any edit. Run:

```bash
rm -rf build-base && \
idf.py -B build-base -DSDKCONFIG=build-base/sdkconfig set-target esp32s3 && \
idf.py -B build-base -DSDKCONFIG=build-base/sdkconfig build && \
grep -A1 -E '\.rodata\.(mic_cfg|spk_cfg)' build-base/esp32-assistant.map \
  > /tmp/board-pins-before.txt && cat /tmp/board-pins-before.txt
```

The default board for an esp32s3 build is now `lugo-s3-nx` (Task 1, Step 10), so this captures the NX's cfg structs. Keep `/tmp/board-pins-before.txt` until Step 6.

- [ ] **Step 2: Give the NX its pins as literals**

In `components/boards/lugo_s3_nx/board_def.c`, replace lines 14-19:

```c
static const i2s_mic_cfg_t mic_cfg = {
    .port = 0, .ws = CONFIG_AA_MIC_WS, .sck = CONFIG_AA_MIC_SCK, .sd = CONFIG_AA_MIC_SD,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1, .bclk = CONFIG_AA_SPK_BCLK, .lrc = CONFIG_AA_SPK_LRC, .din = CONFIG_AA_SPK_DIN,
};
```

with:

```c
// These were CONFIG_AA_MIC_*/CONFIG_AA_SPK_* until 2026-08-03. Those symbols
// were global, and lugo-s3-wroom read the same ones, so the two boards could
// not be given different audio pins — retuning one silently retuned the other.
// The values below are exactly what those symbols defaulted to, i.e. the NX's
// real wiring, now written where the rest of this board's pins live.
#define PIN_MIC_WS    4
#define PIN_MIC_SCK   5
#define PIN_MIC_SD    6
#define PIN_SPK_BCLK 15
#define PIN_SPK_LRC  16
#define PIN_SPK_DIN   7

static const i2s_mic_cfg_t mic_cfg = {
    .port = 0, .ws = PIN_MIC_WS, .sck = PIN_MIC_SCK, .sd = PIN_MIC_SD,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1, .bclk = PIN_SPK_BCLK, .lrc = PIN_SPK_LRC, .din = PIN_SPK_DIN,
};
```

Do not add these to `LUGO_RESERVED_PINS` — the I2S driver reserves its own pins, as the existing comment at the bottom of the file says.

- [ ] **Step 3: Give the WROOM the same values, labelled placeholder**

In `components/boards/lugo_s3_wroom/board_def.c`, replace lines 17-22 with:

```c
// PLACEHOLDER audio pins (copied from lugo-s3-nx), matching the placeholder
// display/button pins below. These were CONFIG_AA_MIC_*/CONFIG_AA_SPK_* until
// 2026-08-03, shared with the NX; they are literals now so this board can be
// given its real pinout without disturbing the NX. Fill in when the board
// exists.
#define PIN_MIC_WS    4
#define PIN_MIC_SCK   5
#define PIN_MIC_SD    6
#define PIN_SPK_BCLK 15
#define PIN_SPK_LRC  16
#define PIN_SPK_DIN   7

static const i2s_mic_cfg_t mic_cfg = {
    .port = 0, .ws = PIN_MIC_WS, .sck = PIN_MIC_SCK, .sd = PIN_MIC_SD,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1, .bclk = PIN_SPK_BCLK, .lrc = PIN_SPK_LRC, .din = PIN_SPK_DIN,
};
```

- [ ] **Step 4: Delete the six Kconfig symbols**

In `main/Kconfig.projbuild`, delete lines 36-54 — the `AA_MIC_WS`, `AA_MIC_SCK`, `AA_MIC_SD`, `AA_SPK_BCLK`, `AA_SPK_LRC`, `AA_SPK_DIN` blocks, including the blank line separating the mic group from the speaker group. Leave `AA_WIFI_PASS` above and `AA_BOOT_COLOR_BARS` below untouched.

- [ ] **Step 5: Confirm no consumer remains**

Run: `grep -rn "CONFIG_AA_MIC_WS\|CONFIG_AA_MIC_SCK\|CONFIG_AA_MIC_SD\|CONFIG_AA_SPK_" main components test`
Expected: no output. (`CONFIG_AA_MIC_METER` is a different symbol and must survive — the pattern above does not match it.)

- [ ] **Step 6: Prove no pin moved**

Run:
```bash
rm -rf build-after && \
idf.py -B build-after -DSDKCONFIG=build-after/sdkconfig set-target esp32s3 && \
idf.py -B build-after -DSDKCONFIG=build-after/sdkconfig build && \
grep -A1 -E '\.rodata\.(mic_cfg|spk_cfg)' build-after/esp32-assistant.map \
  > /tmp/board-pins-after.txt && \
diff <(awk '{print $NF, $(NF-1)}' /tmp/board-pins-before.txt) \
     <(awk '{print $NF, $(NF-1)}' /tmp/board-pins-after.txt) && echo "PINS UNCHANGED"
```
Expected: `PINS UNCHANGED`. The `awk` strips absolute addresses (which shift between builds) and compares section sizes and owning objects.

If the diff is non-empty, stop — a pin value changed. Re-read Step 2 against the Kconfig defaults that were at `main/Kconfig.projbuild:36-54` before deletion (`git show HEAD~1:main/Kconfig.projbuild | sed -n '36,54p'`).

- [ ] **Step 7: Confirm the symbols are gone from generated config**

Run: `grep -c "CONFIG_AA_MIC_WS\|CONFIG_AA_SPK_" build-after/sdkconfig`
Expected: `0`

- [ ] **Step 8: Run the full host test suite**

Run: `make -C test test`
Expected: all binaries `OK`, frame-size check passes.

- [ ] **Step 9: Commit**

```bash
git add components/boards/lugo_s3_nx/board_def.c \
        components/boards/lugo_s3_wroom/board_def.c main/Kconfig.projbuild
git commit -m "fix(board): stop nx and wroom sharing one set of audio pins

AA_MIC_*/AA_SPK_* were global Kconfig symbols that both boards read, so
the two could not have different mic or speaker pins — retuning one
silently retuned the other. Nothing overrode the defaults, so those
values become literals in each board file. Verified pin-for-pin against
the linker map: the NX's mic_cfg and spk_cfg bytes are unchanged."
```

---

### Task 3: Add the `lugo-custom` board

New hardware currently needs a directory, a `board_def.c`, two Kconfig entries and an sdkconfig overlay before it can make a sound. This board removes all of that for the bring-up case: wire it, enter the pins in menuconfig, build.

**Files:**
- Create: `components/boards/lugo_custom/board_def.c`
- Modify: `main/Kconfig.projbuild` (add `AA_BOARD_CUSTOM` to the choice, a name default, and a new pinout menu at the end of the file)

**Interfaces:**
- Consumes: Task 1's `board_t` without `match`; Task 2's freed `AA_MIC_*`/`AA_SPK_*` namespace.
- Produces: a board named `"lugo-custom"`, selected by `CONFIG_AA_BOARD_CUSTOM`. Kconfig symbols `AA_CUSTOM_MIC_WS/SCK/SD`, `AA_CUSTOM_MIC_RIGHT_SLOT`, `AA_CUSTOM_SPK_BCLK/LRC/DIN` (S3); `AA_CUSTOM_FD_BCLK/WS/MIC_DATA/SPK_DATA/MIC_BCLK/MIC_WS` (C3); `AA_CUSTOM_DISP_CLK/DAT/DC/RST/BL` and the `AA_CUSTOM_DISP_AUTO`/`_SSD1306`/`_NONE` choice; `AA_CUSTOM_BTN_WAKE/VOL_UP/VOL_DOWN/EMOTION` (both).

- [ ] **Step 1: Add the board to the Kconfig choice**

In `main/Kconfig.projbuild`, inside `choice AA_BOARD` (after the `AA_BOARD_LUGO_C3_SUPERMINI` block, before `endchoice`), add:

```
config AA_BOARD_CUSTOM
    bool "Custom board — enter the pinout below (bring-up / new hardware)"
    help
        For hardware that has no board_def.c yet. Every pin comes from the
        "Custom board pinout" menu below, so trying a new board needs no new
        files. Once the pinout is proven, promote it to its own
        components/boards/<name>/board_def.c — that file is where the reasons
        behind each choice get written down, which this menu cannot hold.
```

And in `config AA_BOARD_NAME`, add as the last `default` before `default ""`:

```
    default "lugo-custom" if AA_BOARD_CUSTOM
```

- [ ] **Step 2: Add the pinout menu**

Append to the end of `main/Kconfig.projbuild`:

```
menu "Custom board pinout"
    visible if AA_BOARD_CUSTOM

# `visible if` rather than `depends on`: it hides the prompts for anyone
# building a known board while leaving each symbol at its default value. With
# `depends on`, an unmet dependency drives int symbols to 0 — and 0 is a real
# GPIO, so a stray value would look like a deliberate pin rather than an unset
# one.

config AA_CUSTOM_MIC_WS
    int "INMP441 mic I2S WS/LRCK gpio"
    depends on IDF_TARGET_ESP32S3
    range 0 48
    default 4
config AA_CUSTOM_MIC_SCK
    int "INMP441 mic I2S SCK/BCLK gpio"
    depends on IDF_TARGET_ESP32S3
    range 0 48
    default 5
config AA_CUSTOM_MIC_SD
    int "INMP441 mic I2S SD (data out from mic) gpio"
    depends on IDF_TARGET_ESP32S3
    range 0 48
    default 6
config AA_CUSTOM_MIC_RIGHT_SLOT
    bool "INMP441 L/R is tied HIGH (mic drives the RIGHT I2S slot)"
    depends on IDF_TARGET_ESP32S3
    default n
    help
        Leave off when L/R goes to GND, which is the usual wiring. Turn it on
        for a module with L/R tied to 3V3. Reading the wrong slot yields a
        channel of pure zeros that looks exactly like a dead mic, so if the mic
        is silent and the pins are confirmed, try the other setting.

config AA_CUSTOM_SPK_BCLK
    int "MAX98357A speaker I2S BCLK gpio"
    depends on IDF_TARGET_ESP32S3
    range 0 48
    default 15
config AA_CUSTOM_SPK_LRC
    int "MAX98357A speaker I2S LRC/WS gpio"
    depends on IDF_TARGET_ESP32S3
    range 0 48
    default 16
config AA_CUSTOM_SPK_DIN
    int "MAX98357A speaker I2S DIN (data into amp) gpio"
    depends on IDF_TARGET_ESP32S3
    range 0 48
    default 7

config AA_CUSTOM_FD_BCLK
    int "Shared full-duplex I2S BCLK gpio (speaker's bit clock)"
    depends on IDF_TARGET_ESP32C3
    range 0 21
    default 7
config AA_CUSTOM_FD_WS
    int "Shared full-duplex I2S WS gpio (speaker's word select)"
    depends on IDF_TARGET_ESP32C3
    range 0 21
    default 3
config AA_CUSTOM_FD_MIC_DATA
    int "INMP441 SD (data in) gpio"
    depends on IDF_TARGET_ESP32C3
    range 0 21
    default 10
config AA_CUSTOM_FD_SPK_DATA
    int "MAX98357A DIN (data out) gpio"
    depends on IDF_TARGET_ESP32C3
    range 0 21
    default 6
config AA_CUSTOM_FD_MIC_BCLK
    int "Mic's own SCK gpio, or -1 if it shares the speaker's BCLK"
    depends on IDF_TARGET_ESP32C3
    range -1 21
    default -1
    help
        The C3 has one I2S controller. When the mic is physically wired to its
        own SCK/WS pins rather than tied to the speaker's, set these two and the
        controller's clock is duplicated onto them through the GPIO matrix. Set
        both to -1 when the mic shares the speaker's clock pins.
config AA_CUSTOM_FD_MIC_WS
    int "Mic's own WS gpio, or -1 if it shares the speaker's WS"
    depends on IDF_TARGET_ESP32C3
    range -1 21
    default -1

choice AA_CUSTOM_DISP
    prompt "Display this board can carry"
    default AA_CUSTOM_DISP_SSD1306

config AA_CUSTOM_DISP_AUTO
    bool "Either — probe I2C for an SSD1306, else drive an ST7789 on the same pins"
config AA_CUSTOM_DISP_SSD1306
    bool "SSD1306 only (I2C OLED)"
config AA_CUSTOM_DISP_NONE
    bool "No display"
endchoice

config AA_CUSTOM_DISP_CLK
    int "Display SCL (SSD1306) / SCLK (ST7789) gpio"
    depends on !AA_CUSTOM_DISP_NONE
    range 0 48
    default 2
config AA_CUSTOM_DISP_DAT
    int "Display SDA (SSD1306) / MOSI (ST7789) gpio"
    depends on !AA_CUSTOM_DISP_NONE
    range 0 48
    default 13
config AA_CUSTOM_DISP_DC
    int "ST7789 DC gpio"
    depends on AA_CUSTOM_DISP_AUTO
    range 0 48
    default 1
config AA_CUSTOM_DISP_RST
    int "ST7789 RST gpio"
    depends on AA_CUSTOM_DISP_AUTO
    range 0 48
    default 2
config AA_CUSTOM_DISP_BL
    int "ST7789 backlight gpio, or -1 if the panel LED is tied to 3V3"
    depends on AA_CUSTOM_DISP_AUTO
    range -1 48
    default -1

config AA_CUSTOM_BTN_WAKE
    int "Wake button gpio (active low), or -1 if absent"
    range -1 48
    default 7
config AA_CUSTOM_BTN_VOL_UP
    int "Volume-up button gpio (active low), or -1 if absent"
    range -1 48
    default -1
config AA_CUSTOM_BTN_VOL_DOWN
    int "Volume-down button gpio (active low), or -1 if absent"
    range -1 48
    default -1
config AA_CUSTOM_BTN_EMOTION
    int "Emotion button gpio (active low), or -1 if absent"
    range -1 48
    default -1

endmenu
```

The `range 0 48` / `range 0 21` bounds are the S3 and C3 GPIO ranges. The display and button symbols carry the S3 range because they are shared between targets; a C3 build that enters a pin above 21 fails at I2S/GPIO config with a clear IDF error.

- [ ] **Step 3: Write the board**

Create `components/boards/lugo_custom/board_def.c`:

```c
#include "board_common.h"
#include "sdkconfig.h"

#if CONFIG_AA_BOARD_CUSTOM

// The bring-up board: every pin comes from the "Custom board pinout" Kconfig
// menu, so new hardware needs no new file. Unlike every other board here, this
// one deliberately holds no knowledge — there are no pin numbers to explain and
// no wiring quirks recorded, because it is not one board.
//
// When a pinout entered here is proven, promote it: copy these values into a
// components/boards/<name>/board_def.c and write down what was learned getting
// there. That is how lugo-s3-supermini earned its GPIO4 warning, and it is the
// part a Kconfig menu cannot carry.

#ifdef CONFIG_AA_CUSTOM_MIC_RIGHT_SLOT
#define CUSTOM_MIC_RIGHT_SLOT true
#else
#define CUSTOM_MIC_RIGHT_SLOT false
#endif

#if CONFIG_IDF_TARGET_ESP32S3
#include "i2s_mic.h"
#include "i2s_speaker.h"
// Dual I2S: mic on controller 0, speaker on controller 1 — the split every S3
// board here uses. Not exposed in Kconfig; a custom board has no reason to
// differ, and a knob would only be a way to get it wrong.
static const i2s_mic_cfg_t mic_cfg = {
    .port = 0,
    .ws   = CONFIG_AA_CUSTOM_MIC_WS,
    .sck  = CONFIG_AA_CUSTOM_MIC_SCK,
    .sd   = CONFIG_AA_CUSTOM_MIC_SD,
    .right_slot = CUSTOM_MIC_RIGHT_SLOT,
};
static const i2s_speaker_cfg_t spk_cfg = {
    .port = 1,
    .bclk = CONFIG_AA_CUSTOM_SPK_BCLK,
    .lrc  = CONFIG_AA_CUSTOM_SPK_LRC,
    .din  = CONFIG_AA_CUSTOM_SPK_DIN,
};
#define CUSTOM_MIC_OPS     &i2s_mic_ops
#define CUSTOM_SPEAKER_OPS &i2s_speaker_ops
#define CUSTOM_MIC_CFG     &mic_cfg
#define CUSTOM_SPEAKER_CFG &spk_cfg

#elif CONFIG_IDF_TARGET_ESP32C3
#include "i2s_fd.h"
// One I2S controller, shared by RX and TX; both ops back onto this single cfg.
static const i2s_fd_cfg_t fd_cfg = {
    .bclk     = CONFIG_AA_CUSTOM_FD_BCLK,
    .ws       = CONFIG_AA_CUSTOM_FD_WS,
    .mic_data = CONFIG_AA_CUSTOM_FD_MIC_DATA,
    .spk_data = CONFIG_AA_CUSTOM_FD_SPK_DATA,
    .mic_bclk = CONFIG_AA_CUSTOM_FD_MIC_BCLK,
    .mic_ws   = CONFIG_AA_CUSTOM_FD_MIC_WS,
};
#define CUSTOM_MIC_OPS     &i2s_fd_mic_ops
#define CUSTOM_SPEAKER_OPS &i2s_fd_speaker_ops
#define CUSTOM_MIC_CFG     &fd_cfg
#define CUSTOM_SPEAKER_CFG &fd_cfg

#else
#error "lugo-custom supports esp32s3 and esp32c3 only — add an audio branch for this target"
#endif

#if CONFIG_AA_CUSTOM_DISP_AUTO
LUGO_DISPLAY_AUTO(CONFIG_AA_CUSTOM_DISP_CLK, CONFIG_AA_CUSTOM_DISP_DAT,
                  CONFIG_AA_CUSTOM_DISP_DC, CONFIG_AA_CUSTOM_DISP_RST,
                  CONFIG_AA_CUSTOM_DISP_BL);
#define CUSTOM_DISPLAY_CFG &display_cfg
LUGO_RESERVED_PINS(CONFIG_AA_CUSTOM_DISP_CLK, CONFIG_AA_CUSTOM_DISP_DAT,
                   CONFIG_AA_CUSTOM_DISP_DC, CONFIG_AA_CUSTOM_DISP_RST,
                   CONFIG_AA_CUSTOM_DISP_BL,
                   CONFIG_AA_CUSTOM_BTN_WAKE, CONFIG_AA_CUSTOM_BTN_VOL_UP,
                   CONFIG_AA_CUSTOM_BTN_VOL_DOWN, CONFIG_AA_CUSTOM_BTN_EMOTION);

#elif CONFIG_AA_CUSTOM_DISP_SSD1306
// SSD1306 only: display_auto_cfg_t takes .st7789 = NULL, so display_init()
// probes I2C and stops there instead of falling back to a panel that is not
// fitted. DC/RST are not reserved because nothing drives them.
static const display_ssd1306_cfg_t ssd1306_cfg = {
    .scl = CONFIG_AA_CUSTOM_DISP_CLK, .sda = CONFIG_AA_CUSTOM_DISP_DAT,
    .i2c_addr = 0x3C,
};
static const display_auto_cfg_t display_cfg = {
    .ssd1306 = &ssd1306_cfg, .st7789 = NULL,
};
#define CUSTOM_DISPLAY_CFG &display_cfg
LUGO_RESERVED_PINS(CONFIG_AA_CUSTOM_DISP_CLK, CONFIG_AA_CUSTOM_DISP_DAT,
                   CONFIG_AA_CUSTOM_BTN_WAKE, CONFIG_AA_CUSTOM_BTN_VOL_UP,
                   CONFIG_AA_CUSTOM_BTN_VOL_DOWN, CONFIG_AA_CUSTOM_BTN_EMOTION);

#else  // CONFIG_AA_CUSTOM_DISP_NONE
// display_init() checks its cfg for NULL and leaves the headless ops in place.
#define CUSTOM_DISPLAY_CFG NULL
LUGO_RESERVED_PINS(CONFIG_AA_CUSTOM_BTN_WAKE, CONFIG_AA_CUSTOM_BTN_VOL_UP,
                   CONFIG_AA_CUSTOM_BTN_VOL_DOWN, CONFIG_AA_CUSTOM_BTN_EMOTION);
#endif

// A -1 entry in reserved_pins above is inert: no GPIO is -1, so an absent
// button reserves nothing. That is why the button pins are listed
// unconditionally while DC/RST are gated on the panel that uses them.
static const buttons_gpio_cfg_t buttons_cfg = {
    .wake     = CONFIG_AA_CUSTOM_BTN_WAKE,
    .vol_up   = CONFIG_AA_CUSTOM_BTN_VOL_UP,
    .vol_down = CONFIG_AA_CUSTOM_BTN_VOL_DOWN,
    .emotion  = CONFIG_AA_CUSTOM_BTN_EMOTION,
};

LUGO_BOARD_REGISTER(board_lugo_custom) {
    .name        = "lugo-custom",
    .mic         = CUSTOM_MIC_OPS,
    .speaker     = CUSTOM_SPEAKER_OPS,
    .display     = NULL,                  // NULL -> auto-detect (see display_cfg)
    .buttons     = &buttons_gpio_ops,
    .mic_cfg     = CUSTOM_MIC_CFG,
    .speaker_cfg = CUSTOM_SPEAKER_CFG,
    .display_cfg = CUSTOM_DISPLAY_CFG,
    .buttons_cfg = &buttons_cfg,
    LUGO_BOARD_RESERVED_PINS,
};

#endif // CONFIG_AA_BOARD_CUSTOM
```

Note this file is guarded on `CONFIG_AA_BOARD_CUSTOM`, not on the SoC target like the others — it supports both, and compiling it when unselected would register a second board whose pins are menu defaults.

- [ ] **Step 4: Build the custom board on the S3**

Run:
```bash
rm -rf build-custom-s3 && \
idf.py -B build-custom-s3 -DSDKCONFIG=build-custom-s3/sdkconfig set-target esp32s3 && \
echo "CONFIG_AA_BOARD_CUSTOM=y" >> build-custom-s3/sdkconfig && \
idf.py -B build-custom-s3 -DSDKCONFIG=build-custom-s3/sdkconfig reconfigure build
```
Expected: build succeeds. `idf.py reconfigure` is required because `components/boards/CMakeLists.txt` globs board directories and a new one was added.

- [ ] **Step 5: Confirm the board registered**

Run: `grep -c "board_lugo_custom" build-custom-s3/esp32-assistant.map`
Expected: a non-zero count — the board's `.rodata.board_lugo_custom` symbol is in the link.

- [ ] **Step 6: Build the custom board on the C3**

Run:
```bash
rm -rf build-custom-c3 && \
idf.py -B build-custom-c3 -DSDKCONFIG=build-custom-c3/sdkconfig set-target esp32c3 && \
echo "CONFIG_AA_BOARD_CUSTOM=y" >> build-custom-c3/sdkconfig && \
idf.py -B build-custom-c3 -DSDKCONFIG=build-custom-c3/sdkconfig reconfigure build
```
Expected: build succeeds, exercising the `i2s_fd` branch.

- [ ] **Step 7: Build the two non-default display options**

Run, for each of `CONFIG_AA_CUSTOM_DISP_AUTO=y` and `CONFIG_AA_CUSTOM_DISP_NONE=y`:
```bash
sed -i.bak 's/^CONFIG_AA_CUSTOM_DISP_SSD1306=y/# CONFIG_AA_CUSTOM_DISP_SSD1306 is not set/' build-custom-s3/sdkconfig && \
echo "CONFIG_AA_CUSTOM_DISP_AUTO=y" >> build-custom-s3/sdkconfig && \
idf.py -B build-custom-s3 -DSDKCONFIG=build-custom-s3/sdkconfig reconfigure build
```
Expected: both build. This covers all three branches of the `#if CONFIG_AA_CUSTOM_DISP_*` block — the `NONE` branch in particular must compile with `.display_cfg = NULL`.

- [ ] **Step 8: Confirm the known boards still build unchanged**

Run:
```bash
rm -rf build-verify && \
idf.py -B build-verify -DSDKCONFIG=build-verify/sdkconfig set-target esp32s3 && \
idf.py -B build-verify -DSDKCONFIG=build-verify/sdkconfig build && \
grep -c "board_lugo_custom" build-verify/esp32-assistant.map
```
Expected: build succeeds and the grep prints `0` — with `AA_BOARD_CUSTOM` unselected, the custom board must not be in the binary at all.

- [ ] **Step 9: Run the full host test suite**

Run: `make -C test test`
Expected: all binaries `OK`, frame-size check passes.

- [ ] **Step 10: Commit**

```bash
git add components/boards/lugo_custom main/Kconfig.projbuild
git commit -m "feat(board): add lugo-custom, a Kconfig-driven bring-up board

New hardware needed a directory, a board_def.c and two Kconfig entries
before it could make a sound. This board takes every pin from menuconfig
instead, so trying a new wiring needs no new files. Known boards keep
their hand-written pin maps, because half of what those files contain is
reasoning a Kconfig help block cannot hold."
```

---

### Task 4: Compile only the selected board

Optional and independently droppable — see Step 1. The payoff is not the ~390 bytes of unused board structs; it is that a wrong board name fails at configure time naming the missing directory, instead of booting and logging `no board named "..."`.

**Files:**
- Modify: `components/boards/CMakeLists.txt:1-8`

**Interfaces:**
- Consumes: Task 3's `lugo_custom` directory and `CONFIG_AA_BOARD_NAME` values from Task 1's Kconfig.
- Produces: no C-level interface change. `libboards.a` contains exactly one `board_def.c.obj`.

- [ ] **Step 1: Probe the risk before writing anything**

ESP-IDF parses component `CMakeLists.txt` twice, and `CONFIG_*` is unavailable in the early requirements-extraction pass. Confirm it *is* available in the real pass. Add this line temporarily at the top of `components/boards/CMakeLists.txt`:

```cmake
message(STATUS "BOARDS-PROBE: AA_BOARD_NAME=[${CONFIG_AA_BOARD_NAME}]")
```

Run:
```bash
rm -rf build-probe && \
idf.py -B build-probe -DSDKCONFIG=build-probe/sdkconfig set-target esp32s3 && \
idf.py -B build-probe -DSDKCONFIG=build-probe/sdkconfig reconfigure 2>&1 | grep BOARDS-PROBE
```

Expected: at least two lines, the last showing `AA_BOARD_NAME=[lugo-s3-nx]`. An early line showing `AA_BOARD_NAME=[]` is the script-mode pass and is fine — the `else` branch in Step 2 handles it.

**If every line shows an empty value, stop.** Remove the probe line, skip to Task 5, and note in the commit that Task 4 was dropped. Tasks 1-3 do not depend on it.

- [ ] **Step 2: Replace the glob**

Remove the probe line and replace lines 1-8 of `components/boards/CMakeLists.txt` (the comment block and the `file(GLOB ...)`) with:

```cmake
# Only the selected board is compiled. The directory name is CONFIG_AA_BOARD_NAME
# with dashes swapped for underscores ("lugo-s3-nx" -> "lugo_s3_nx"), so a board
# name with no matching directory fails here, at configure time, naming the path
# it could not find — rather than building fine and failing at boot.
#
# The else branch is for ESP-IDF's requirements-extraction pass, which parses
# this file in CMake script mode where no CONFIG_* is defined yet. SRCS is
# ignored in that pass; the glob just gives it something well-formed.
if(CONFIG_AA_BOARD_NAME)
    string(REPLACE "-" "_" _board_dir "${CONFIG_AA_BOARD_NAME}")
    set(BOARD_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/${_board_dir}/board_def.c")
else()
    file(GLOB BOARD_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*/board_def.c")
endif()
```

Leave `idf_component_register(...)` and the `WHOLE_ARCHIVE` comment below it untouched — a single unreferenced board object still needs forcing into the link.

- [ ] **Step 3: Verify one board is compiled**

Run:
```bash
rm -rf build-one && \
idf.py -B build-one -DSDKCONFIG=build-one/sdkconfig set-target esp32s3 && \
idf.py -B build-one -DSDKCONFIG=build-one/sdkconfig build && \
grep -c "^ board_desc " build-one/esp32-assistant.map
```
Expected: `1` (it was `3` before this task).

- [ ] **Step 4: Verify a wrong name fails at configure time**

Run:
```bash
sed -i.bak 's/^CONFIG_AA_BOARD_NAME=.*/CONFIG_AA_BOARD_NAME="lugo-nonexistent"/' build-one/sdkconfig && \
idf.py -B build-one -DSDKCONFIG=build-one/sdkconfig reconfigure 2>&1 | tail -20
```
Expected: a CMake error naming `components/boards/lugo_nonexistent/board_def.c`. Restore with `mv build-one/sdkconfig.bak build-one/sdkconfig`.

- [ ] **Step 5: Verify every board still builds**

Run each of these, and the same for esp32c3 with `lugo-c3-devkit`, `lugo-c3-supermini` and `lugo-custom`:

```bash
for b in lugo-s3-nx lugo-s3-supermini lugo-s3-wroom; do
  rm -rf build-each && \
  idf.py -B build-each -DSDKCONFIG=build-each/sdkconfig set-target esp32s3 && \
  sed -i.bak "s/^CONFIG_AA_BOARD_NAME=.*/CONFIG_AA_BOARD_NAME=\"$b\"/" build-each/sdkconfig && \
  idf.py -B build-each -DSDKCONFIG=build-each/sdkconfig reconfigure build || { echo "FAILED: $b"; break; }
done
```
Expected: all three build. Note `CONFIG_AA_BOARD_NAME` is a hidden string derived from the choice, so setting the corresponding `CONFIG_AA_BOARD_LUGO_*=y` is the equivalent through menuconfig.

- [ ] **Step 6: Run the full host test suite**

Run: `make -C test test`
Expected: all binaries `OK`. (Host tests do not use CMake, so this is a regression guard rather than a check of this task.)

- [ ] **Step 7: Commit**

```bash
git add components/boards/CMakeLists.txt
git commit -m "build(board): compile only the selected board

A board name with no matching directory now fails at configure time
naming the path, instead of building fine and reporting a missing board
at boot. Dropping the four unused board objects saves ~390 bytes, which
is not why this is worth doing."
```

---

### Task 5: Document the custom board

**Files:**
- Create: `docs/custom-board.md`
- Modify: `README.md` (add a pointer in the board section)

**Interfaces:**
- Consumes: everything from Tasks 1-4.
- Produces: no code interface.

- [ ] **Step 1: Write the doc**

Create `docs/custom-board.md`:

````markdown
# Bringing up a board that has no board_def.c

`lugo-custom` exists so new hardware can make a sound before anyone writes a
file for it. Every pin comes from Kconfig.

## Use it

```
idf.py set-target esp32s3      # or esp32c3
idf.py menuconfig
```

Under **Target board**, pick *Custom board*. A **Custom board pinout** menu
appears below; enter the GPIOs you actually wired. Then build and flash as
usual.

If the mic is silent and the pins are confirmed correct, try toggling
*INMP441 L/R is tied HIGH*. Reading the wrong I2S slot returns a channel of
pure zeros, which looks exactly like a dead microphone.

`CONFIG_AA_MIC_METER` (under the project's own menu) renders the mic's peak
level on the panel, which separates "mic produces nothing" from "the rest of
the pipeline is wrong".

## Then promote it

The custom board is a workbench, not a destination. Once a pinout works,
copy it into `components/boards/<your_board>/board_def.c`, add the matching
entries to `choice AA_BOARD` and `AA_BOARD_NAME` in `main/Kconfig.projbuild`,
and — the part that matters — write down what you learned.

`components/boards/lugo_s3_supermini/board_def.c` is the example to follow. It
records that GPIO4 on that board is shorted to ground and fails silently as a
logic 0, that GPIO44 is safe for BCLK only because the console runs on the
native USB-Serial-JTAG rather than UART0, and that its INMP441 drives the right
slot as measured rather than assumed. None of that fits in a Kconfig help
block, and all of it cost hardware debugging to find.

## What cannot be auto-detected

The **MAX98357A** cannot be detected at all. Every digital pin it has is an
input and its only output is analogue, so there is no signal path back to the
ESP32 — no software can tell whether one is attached.

The **INMP441** drives its data line, so a stereo capture can reveal which slot
it uses and whether any mic is responding. But it cannot identify the part (an
ICS-43434 looks identical) and it cannot find the pins, because probing means
driving clocks on pins you must already have chosen. That is why this menu asks
you for them.
````

- [ ] **Step 2: Link it from the README**

In `README.md`, near the existing build instructions (around line 78, after the `idf.py menuconfig` block), add:

```markdown
Pick your board under **Target board**. For hardware with no board definition
yet, choose *Custom board* and enter the pinout — see
[docs/custom-board.md](docs/custom-board.md).
```

- [ ] **Step 3: Check the doc against the code**

Run: `grep -n "AA_CUSTOM_MIC_RIGHT_SLOT\|AA_MIC_METER" main/Kconfig.projbuild`
Expected: both symbols exist, with prompts matching the names quoted in `docs/custom-board.md`. Fix the doc if a prompt was reworded.

- [ ] **Step 4: Commit**

```bash
git add docs/custom-board.md README.md
git commit -m "docs: how to bring up a board with no board_def.c

Covers the custom board's menu, the right-slot trap that makes a working
mic look dead, and the promote-it-afterwards step where the reasoning
gets written down. Also records why the amp can never be autodetected,
so it stops being re-proposed."
```

---

## Verification checklist

Run after all tasks, before merging:

- [ ] `make -C test test` — 13 binaries `OK`, MCP frame-size check passes
- [ ] Fresh esp32s3 build for `lugo-s3-nx`, `lugo-s3-supermini`, `lugo-s3-wroom`, `lugo-custom`
- [ ] Fresh esp32c3 build for `lugo-c3-devkit`, `lugo-c3-supermini`, `lugo-custom`
- [ ] `grep -rn "CONFIG_AA_MIC_WS\|CONFIG_AA_SPK_" main components test` → no output
- [ ] `grep -rn "match" components/boards/*/board_def.c` → no output
- [ ] NX `mic_cfg`/`spk_cfg` map bytes unchanged versus pre-Task-2 baseline

**Hardware verification** (S3 SuperMini, the unit on hand). This is the test that
proves the custom path is real rather than merely compiling:

- [ ] Flash as `lugo-s3-supermini`; confirm mic capture and TTS playback work
- [ ] Flash as `lugo-custom` with the same pins entered in menuconfig — mic WS 8,
      SCK 5, SD 6, *L/R tied HIGH* on; speaker LRC 1, BCLK 44, DIN 12; display
      SSD1306-only on CLK 2 / DAT 13; wake button 7 — and confirm identical
      behaviour
