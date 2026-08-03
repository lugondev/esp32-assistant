# Board pinout config: a Kconfig-driven custom board, and honest board selection

## Problem

Adding a board today means touching five places: a new `components/boards/<name>/`
directory, its `board_def.c`, an entry in the `choice AA_BOARD`, a line in
`AA_BOARD_NAME`, and a `sdkconfig.defaults.<board>` overlay. That is a lot of
ceremony for the common case of "I wired an INMP441 to a new dev board and want
to hear whether it works."

The tempting fix — move every pin out of `board_def.c` into Kconfig — was
considered and rejected. See Rejected alternatives.

Three things prompted this work:

1. Changing a pin should not require editing C.
2. Trying a new board should not require creating files.
3. The binary should not carry boards it will never run.

A fourth item surfaced while investigating and is folded in: a half-finished
version of (1) already exists and is broken — `lugo-s3-nx` and `lugo-s3-wroom`
share one global set of mic/speaker pin symbols and so cannot be given
different pinouts. See Part 1.

## What the measurements say

Before designing around (3), it was measured. In `build-s3sm`, the linker map
(`esp32-assistant.map`) shows only three `board_desc` entries, not five: the
`#if CONFIG_IDF_TARGET_ESP32C3` guard already compiles the two C3 boards to
nothing in an S3 build. The two *unused* S3 boards cost:

| symbol | bytes |
|---|---|
| `board_lugo_s3_nx` + its cfg structs + name | ~195 |
| `board_lugo_s3_wroom` + its cfg structs + name | ~194 |

≈ **390 bytes**, against a 3.2 MB app partition with ~800 KB spare — 0.05%.

So (3) is not a real motivation. It is addressed here only because it falls out
of a change made for a better reason (see Part 3), not because 390 bytes matter.

A second measurement, from reading the code rather than the map: **auto-detection
does not exist.** All five `match()` implementations are compile-time constants —
`return true` in `lugo_s3_nx` and `lugo_c3_devkit`, `return false` in the other
three. None probes hardware. `sdkconfig.defaults.s3supermini:33` states why:

> the SuperMini and the NX both answer the same SSD1306 I2C probe, so autodetect
> cannot tell them apart — and their pinouts share nothing.

`CONFIG_AA_BOARD_AUTODETECT` is therefore a menu option that selects "whichever
board happens to be linked first and hardcoded `true`". That is worse than no
option, because it reads like a feature.

## Design

Three parts. Part 1 is the one that delivers the motivating value; Parts 2 and 3
delete the machinery that Part 1 makes it obvious nobody was using.

### Part 1 — a `lugo-custom` board whose pins come from Kconfig

One new board, `components/boards/lugo_custom/board_def.c`, containing no pin
literals. Every pin is a `CONFIG_AA_CUSTOM_*` symbol:

```c
static const i2s_mic_cfg_t mic_cfg = {
    .port = 0,
    .ws   = CONFIG_AA_CUSTOM_MIC_WS,
    .sck  = CONFIG_AA_CUSTOM_MIC_SCK,
    .sd   = CONFIG_AA_CUSTOM_MIC_SD,
    .right_slot = CONFIG_AA_CUSTOM_MIC_RIGHT_SLOT,
};
```

The Kconfig symbols live in a `menu "Custom board pinout"` gated by
`visible if AA_BOARD_CUSTOM`, so they never clutter menuconfig for anyone
building a known board.

**Symbols.** The audio set differs by SoC because the drivers do — dual-I2S on
S3 (`i2s_mic_ops` + `i2s_speaker_ops`), single full-duplex on C3
(`i2s_fd_mic_ops` + `i2s_fd_speaker_ops` sharing one `i2s_fd_cfg_t`). Each group
carries the matching `depends on IDF_TARGET_*`.

*S3 (dual I2S):*
`AA_CUSTOM_MIC_WS`, `AA_CUSTOM_MIC_SCK`, `AA_CUSTOM_MIC_SD`,
`AA_CUSTOM_MIC_RIGHT_SLOT` (bool), `AA_CUSTOM_SPK_BCLK`, `AA_CUSTOM_SPK_LRC`,
`AA_CUSTOM_SPK_DIN`.

I2S port numbers are *not* exposed. Every S3 board in this tree uses mic on
port 0 and speaker on port 1, and a custom board has no reason to differ; an
exposed knob here would be a way to get it wrong, not a capability.

*C3 (single full-duplex I2S):*
`AA_CUSTOM_FD_BCLK`, `AA_CUSTOM_FD_WS`, `AA_CUSTOM_FD_MIC_DATA`,
`AA_CUSTOM_FD_SPK_DATA`, `AA_CUSTOM_FD_MIC_BCLK`, `AA_CUSTOM_FD_MIC_WS`.
The last two default to `-1` (mic shares the speaker's clock physically, no
GPIO-matrix fan-out), matching `i2s_fd_cfg_t`'s documented convention.

*Display (both SoCs):* `AA_CUSTOM_DISP_CLK`, `AA_CUSTOM_DISP_DAT`, plus a
`choice` for what the board can carry:

- `AA_CUSTOM_DISP_AUTO` — SSD1306 probed first, ST7789 fallback. Additionally
  needs `AA_CUSTOM_DISP_DC`, `AA_CUSTOM_DISP_RST`, `AA_CUSTOM_DISP_BL`
  (`-1` where the panel's LED is tied to 3V3).
- `AA_CUSTOM_DISP_SSD1306` — I2C OLED only; emits `.st7789 = NULL`.
- `AA_CUSTOM_DISP_NONE` — `.display_cfg = NULL`.

The three map onto `display_auto_cfg_t`'s existing "either sub-cfg may be NULL"
contract; no display-layer change is needed.

*Buttons:* `AA_CUSTOM_BTN_WAKE`, `AA_CUSTOM_BTN_VOL_UP`, `AA_CUSTOM_BTN_VOL_DOWN`,
`AA_CUSTOM_BTN_EMOTION`, each `-1` for absent — the convention every existing
`buttons_gpio_cfg_t` already uses.

**Reserved pins.** `LUGO_RESERVED_PINS(...)` takes the same `CONFIG_AA_CUSTOM_*`
symbols the cfg structs use, so the two cannot drift — the same discipline
`board_common.h` already demands of hand-written boards. The DC/RST entries are
`#if`-gated on `AA_CUSTOM_DISP_AUTO` so an SSD1306-only custom board does not
reserve two pins it is not driving. A `-1` entry is inert (no GPIO is `-1`), so
absent buttons need no gating.

**Validation.** Each pin symbol gets a Kconfig `range 0 48` (S3) / `range 0 21`
(C3), and the optional ones `range -1 N`. This is validation the current
`#define` approach does not have.

**Reclaiming the existing global pin symbols.** `AA_MIC_WS/SCK/SD` and
`AA_SPK_BCLK/LRC/DIN` already exist (`Kconfig.projbuild:36-54`), and both
`lugo_s3_nx/board_def.c:15,18` and `lugo_s3_wroom/board_def.c:18,21` read them.
This is a half-finished version of the idea, and it is actively broken: the
symbols are global, so those two boards **cannot have different mic or speaker
pins**. Changing one to suit the other silently breaks the other — the "one
sdkconfig value per build" problem, already present in the tree.

Nothing overrides them today. Every `sdkconfig*` file in the repo carries the
same defaults (`4/5/6`, `15/16/7`), and no other translation unit reads them, so
the migration is mechanical:

- Rename to `AA_CUSTOM_MIC_*` / `AA_CUSTOM_SPK_*` and move them inside the
  `visible if AA_BOARD_CUSTOM` menu, where per-build-single-value is correct
  because only one board ever reads them.
- `lugo-s3-nx` gets the current default values as literals — `4/5/6` mic,
  `15/16/7` speaker — which are its real pins.
- `lugo-s3-wroom` gets the same values as literals, labelled placeholder,
  matching how its display pins are already `PLACEHOLDER (copied from
  lugo-s3-nx)`. The board is a foundation with no hardware behind it yet.

**The lifecycle this enables.** New hardware runs on `lugo-custom` and is tuned
in menuconfig until it works. Once it works, it gets *promoted* to its own
`board_def.c` — and that file is where the hard-won knowledge is written down.
This is exactly how `lugo_s3_supermini/board_def.c` earned its GPIO4 warning.
The custom board is a workbench, not a destination.

### Part 2 — delete auto-detection

`match()` selects nothing (see Measurements). Remove it:

- Drop `bool (*match)(void)` from `board_t` (`board_types.h`).
- Delete the one-line `static bool match(void)` and `.match = match,` from all
  five existing `board_def.c` files.
- `board_select()` becomes name-lookup only: return the board whose `name`
  equals `forced_name`, else `NULL`. Both the "first board whose `match()` is
  true" loop and the `return boards[0]` fallback go away.
- Kconfig: delete `config AA_BOARD_AUTODETECT` and `config AA_BOARD_FORCE`
  (which was only ever `!AUTODETECT`). Give `choice AA_BOARD` explicit
  per-target defaults instead:
  `default AA_BOARD_LUGO_S3_NX if IDF_TARGET_ESP32S3`,
  `default AA_BOARD_LUGO_C3_DEVKIT if IDF_TARGET_ESP32C3`.
- Add `config AA_BOARD_CUSTOM` to the choice, with
  `default "lugo-custom" if AA_BOARD_CUSTOM` in `AA_BOARD_NAME`.
- `board.c` drops the `#ifdef CONFIG_AA_BOARD_FORCE` branch; the name is now
  always set, so `board_detect_and_select()` becomes a straight lookup and its
  failure log loses the `forced ? forced : "auto"` ternary.

The `board_desc` section registry, `linker.lf`, and `WHOLE_ARCHIVE` all stay.
They are already debugged (the `ALIGN(4)` comment records a real crash) and are
what still makes a board file self-registering.

### Part 3 — compile only the selected board

`components/boards/CMakeLists.txt` currently globs every `*/board_def.c`.
Replace with a direct path derived from the selected board:

```cmake
if(CONFIG_AA_BOARD_NAME)
    string(REPLACE "-" "_" _board_dir "${CONFIG_AA_BOARD_NAME}")
    set(BOARD_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/${_board_dir}/board_def.c")
else()
    # Requirements-extraction runs this file in CMake script mode, where no
    # CONFIG_* is defined yet and SRCS is ignored anyway. Glob so the pass has
    # something well-formed to chew on.
    file(GLOB BOARD_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*/board_def.c")
endif()
```

The payoff is not the ~390 bytes. It is that **a wrong board name now fails at
configure time** with a missing-source error naming the directory, instead of
booting and logging `no board selected` from `board_detect_and_select()`. It
also drops the "add a board folder, then remember to `idf.py reconfigure`"
footgun the current glob comment documents, since the path is no longer globbed.

Adding a board still needs its Kconfig entries, so nothing becomes *less*
discoverable.

**Risk to verify first.** ESP-IDF parses component `CMakeLists.txt` twice, and
`CONFIG_*` is unavailable in the early requirements-extraction pass. The `else`
branch above is the guard, but this must be confirmed empirically on a clean
build directory (not an incremental one, which may hide it) before the rest of
Part 3 is considered done. If it turns out `CONFIG_AA_BOARD_NAME` is also unset
in the *real* pass, Part 3 is dropped and Parts 1–2 ship on their own — they do
not depend on it.

## Rejected alternatives

**Move every board's pins into Kconfig.** This was the original proposal. Two
things kill it.

*It has nowhere to put the reasoning.* `lugo_s3_supermini/board_def.c` is 96
lines, of which roughly half is knowledge that cost hardware debugging to
acquire: GPIO4 is shorted to ground on this board and fails silently as a logic
0; GPIO44 is safe for BCLK *because* the console runs on USB-Serial-JTAG rather
than UART0; `.right_slot = true` was measured, not assumed (`|L|max=0`,
`|R|max=0x01a78400`). A Kconfig `help` block is not where that survives — it is
read once in a menu, not while looking at the pin it explains.

*Not every board fact is a number.* `.st7789 = NULL` (the SuperMini header has
too few pins to commit two to an unfitted panel) and the C3's single `fd_cfg`
struct aliased into both `.mic_cfg` and `.speaker_cfg` have no Kconfig
representation. Encoding them would mean inventing enum symbols that a C
initialiser already expresses directly.

*And the practical gain is small.* Editing `#define PIN_MIC_WS 8` then building
is not more work than editing menuconfig then building. Changing `sdkconfig`
in fact rebuilds *more* — `sdkconfig.h` is included nearly everywhere, whereas
`board_def.c` is one translation unit. The genuine Kconfig advantages are the
menuconfig UI and `range` validation, and those matter precisely when there is
no `board_def.c` yet — which is what Part 1 provides.

**Auto-detect the peripherals instead of the board** — probe for the MAX98357A
and the INMP441 and derive the pinout from what answers. This was considered as
a way to keep a real `match()` rather than deleting it. It does not work, for
two different reasons.

*The MAX98357A cannot be detected at all.* Every digital pin it has — LRC,
BCLK, DIN, GAIN, SD_MODE — is an input; its only output is analogue, to the
speaker. There is no I2C, no readback, no signal path of any kind back to the
ESP32. No software can observe whether it is present. The one indirect route is
playing a tone and listening on the mic, which confirms "sound happened in the
room", not "a MAX98357A is on these three pins" — and it presupposes a working
mic, i.e. the other half of what you wanted to detect.

*The INMP441 is partly detectable, but not in the way that would help.* It
drives SD, so there is a return path, and two things genuinely fall out of a
stereo capture: **which slot it drives** (`right_slot` — this is exactly the
hand measurement that solved the SuperMini, `|L|max=0` vs
`|R|max=0x01a78400`), and **whether any mic is responding at all** on a known
pin set. But it cannot identify the part — an ICS-43434 or SPH0645 looks
identical — and it cannot discover pins, because probing requires already
knowing which pins to drive. Scanning blind is ~6840 WS×SCK×SD combinations
across ~20 GPIOs, each needing an I2S init/teardown, while driving clocks onto
pins that may be wired to something else.

That last point is what closes the question: board selection exists to
determine the pinout, and every available probe needs the pinout as an input.
The dependency runs the wrong way, so Part 2 stands.

A narrower idea — a bring-up-only `right_slot` probe, in the spirit of the
existing `CONFIG_AA_MIC_METER` — was raised and **deliberately declined**.
`right_slot` stays hand-declared. It is a soldering fact that never changes
after a board is built, `AA_MIC_METER` already answers "is the mic producing
anything", and the slot itself is cheap enough to determine by hand on the rare
occasion it is wrong. Recorded here so the idea does not get re-proposed as new.

**Drop the `board_desc` section registry entirely** (have each board export a
known symbol, deleting `linker.lf`, `WHOLE_ARCHIVE`, and `board_select.c`).
Tempting once only one board is compiled, but it buys nothing Part 3 has not
already bought, and it discards a mechanism whose one sharp edge is already
found, fixed, and documented.

## Testing

**Host tests** (`test/`, `make -C test`):

- `test_board_select.c`: delete `test_auto_picks_first_match` and
  `test_auto_no_match_falls_back_to_first`; drop `.match` from the fixtures;
  add a case asserting an unknown name returns `NULL` rather than `boards[0]`
  (the old fallback silently returned a wrong board — the new behaviour is the
  point of the change and needs a test that would fail if it regressed).
- `test_board_facades.c` needs no change — a grep over `test/` shows `.match`
  appears only in `test_board_select.c`. All other host tests must still pass
  unchanged.

**Build verification** — every configuration must configure and build clean from
a fresh build directory:

| target | board |
|---|---|
| esp32s3 | `lugo-s3-nx`, `lugo-s3-supermini`, `lugo-s3-wroom`, `lugo-custom` |
| esp32c3 | `lugo-c3-devkit`, `lugo-c3-supermini`, `lugo-custom` |

Plus one negative check: an `AA_BOARD_NAME` pointing at a nonexistent directory
must fail at configure time, confirming Part 3's stated benefit.

**Pin-migration equivalence.** Moving `lugo-s3-nx` off the global
`AA_MIC_*`/`AA_SPK_*` symbols must not change a single pin. Build `lugo-s3-nx`
before and after, and confirm the `.rodata.mic_cfg` and `.rodata.spk_cfg` bytes
in `esp32-assistant.map` are identical. A refactor that silently moves the NX's
mic to a different GPIO would present exactly as the silent-mic failure this
tree has already spent a day on.

Also confirm no `CONFIG_AA_MIC_*` / `CONFIG_AA_SPK_*` remains in any generated
`sdkconfig*` after the rename, so a stale value cannot be mistaken for a live
setting.

**Hardware verification.** The S3 SuperMini is the unit on hand. Build it twice —
once as `lugo-s3-supermini`, once as `lugo-custom` with the same pins entered in
menuconfig (WS 8, SCK 5, SD 6, right-slot on; SPK LRC 1 / BCLK 44 / DIN 12;
display SSD1306-only on 2/13; wake 7) — and confirm mic capture and TTS playback
behave identically. This is the test that proves the custom path is real rather
than merely compiling.

## Out of scope

- Camera facade (`board_t` has no camera member yet).
- The `lugo-s3-wroom` placeholder pins.
- Per-SoC policy currently living in `main.c`'s `#if` blocks. That was
  considered before and deliberately left alone: those are SoC properties, not
  board properties, and moving them into `board_t` would put them in the wrong
  place.
