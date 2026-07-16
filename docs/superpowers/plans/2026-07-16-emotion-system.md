# 28-State Emotion System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand the `robot_eyes` component from 8 to 28 emotion states (per spec `docs/superpowers/specs/2026-07-16-emotion-system-design.md`) with per-emotion blink/motion/drop, plus an HTML preview.

**Architecture:** An extended parametric table on top of the existing shape engine (squircle + glow + wedge), adding a bottom-cut for the arc family, motion offsets (shake/bounce/oscillate) and drops (sweat/tear) — all pure functions of `now_ms`. The old enum values keep their positions.

**Tech Stack:** C11 (ESP-IDF component, host-tested via `test/Makefile`), plain HTML+Canvas (no CDNs).

## Global Constraints

- The 8 existing enum values keep their names + order; 20 new values are appended; `ROBOT_EMOTION_COUNT == 28`.
- No RNG, no mutable state — the same `(emotion, now_ms)` → the same frame.
- `ROBOT_EYES_MAX_REACH_PCT = 140` unchanged; every emotion (including motion/drops) must stay inside the dirty band — enforced by a canary test.
- `robot_eyes_is_closed(t)` ≡ `robot_eyes_is_closed_for(ROBOT_EMOTION_NEUTRAL, t)`; NEUTRAL uses exactly the two old blink constants (3000/150).
- Do not touch `main.c`; do not change the signatures of `robot_eyes_render`/`robot_eyes_dirty_band`/the decor API.
- Tests run with: `cd test && make test_robot_eyes && ./test_robot_eyes` (expected: `ALL PASS`).
- The 28-entry parameter table takes its **exact values** from spec section 4.

---

### Task 1: 28-state enum + full table + per-emotion blink

**Files:**
- Modify: `components/robot_eyes/include/robot_eyes.h` (enum, `robot_eyes_is_closed_for` declaration)
- Modify: `components/robot_eyes/robot_eyes.c` (extended structs, 28-entry table, table-driven blink)
- Test: `test/test_robot_eyes.c`

**Interfaces:**
- Produces: `robot_emotion_t` with 28 values + `ROBOT_EMOTION_COUNT`; `bool robot_eyes_is_closed_for(robot_emotion_t, uint32_t now_ms)`; internal struct `emotion_params_t` with fields `bottom_cut_pct` (per-eye), `blink_interval_ms`, `blink_duration_ms`, `motion`, `motion_amp_pct`, `drop` (Tasks 2–4 render them).

- [x] **Step 1: Write failing tests** — add to `test/test_robot_eyes.c`:

```c
static void test_is_closed_for_neutral_matches_legacy(void) {
    for (uint32_t t = 0; t < 6200; t += 37)
        CHECK(robot_eyes_is_closed(t) == robot_eyes_is_closed_for(ROBOT_EMOTION_NEUTRAL, t));
}

static void test_per_emotion_blink_schedules(void) {
    // BORED: 7000/400 — slow, long closed
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 0) == true);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 399) == true);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 400) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 6999) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 7000) == true);
    // ANXIOUS: 1400/100 — fast
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_ANXIOUS, 99) == true);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_ANXIOUS, 100) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_ANXIOUS, 1400) == true);
    // SQUINT: 9000/150 — hardly ever blinks
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_SQUINT, 150) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_SQUINT, 8999) == false);
}

static void test_every_emotion_paints_something_when_open(void) {
    // Catches an empty/all-zero table entry: every emotion, at an instant
    // its eyes are open, must paint at least one EYE pixel.
    for (int e = 0; e < ROBOT_EMOTION_COUNT; e++) {
        uint32_t t = 500;   // outside every entry's blink window (max duration 400)
        dclear();
        robot_eyes_render(dbuf, DW, DH, DH, 0, t, (robot_emotion_t)e, EYE, BG);
        int lit = 0;
        for (int i = 0; i < DW * DH; i++) if (dbuf[i] == EYE) lit++;
        CHECK(lit > 0);
    }
}

// Times covering the distinct phases: open/closed across the various blink
// schedules, shake (120ms cycle), bounce (600ms), oscillate (250ms), sweat
// pulse (800ms), tear slide (1200ms — deepest at ph=1199, i.e. t=5999).
static const uint32_t SAMPLE_TIMES[] = {
    0, 59, 60, 149, 150, 199, 250, 399, 500, 650, 899,
    1300, 2100, 3049, 4400, 5999, 7100, 8999
};
#define N_SAMPLE_TIMES (sizeof SAMPLE_TIMES / sizeof SAMPLE_TIMES[0])

static void test_all_emotions_stay_inside_dirty_band(void) {
    // Render the full panel; every row OUTSIDE the dirty band must be pure
    // BG for every emotion at every sampled time — an emotion (or a later
    // task's motion/drop) that overflows the band fails here instead of
    // silently clipping on the real screen.
    int band_y, band_h;
    robot_eyes_dirty_band(DH, &band_y, &band_h);
    for (int e = 0; e < ROBOT_EMOTION_COUNT; e++) {
        for (unsigned ti = 0; ti < N_SAMPLE_TIMES; ti++) {
            dclear();
            robot_eyes_render(dbuf, DW, DH, DH, 0, SAMPLE_TIMES[ti], (robot_emotion_t)e, EYE, BG);
            for (int y = 0; y < DH; y++) {
                if (y >= band_y && y < band_y + band_h) continue;
                for (int x = 0; x < DW; x++) CHECK(dpx(x, y) == BG);
            }
        }
    }
}

static void test_render_is_deterministic(void) {
    static uint16_t a[DW * DH], b[DW * DH];
    robot_eyes_render(a, DW, DH, DH, 0, 1234, ROBOT_EMOTION_LAUGHING, EYE, BG);
    robot_eyes_render(b, DW, DH, DH, 0, 1234, ROBOT_EMOTION_LAUGHING, EYE, BG);
    for (int i = 0; i < DW * DH; i++) CHECK(a[i] == b[i]);
}

static void test_asym_emotions_differ_between_eyes(void) {
    robot_emotion_t asym[] = { ROBOT_EMOTION_SKEPTICAL, ROBOT_EMOTION_SUSPICIOUS,
                               ROBOT_EMOTION_ANNOYED, ROBOT_EMOTION_UNIMPRESSED };
    for (unsigned k = 0; k < 4; k++) {
        dclear();
        robot_eyes_render(dbuf, DW, DH, DH, 0, 500, asym[k], EYE, BG);
        int left_cx = DW / 4, right_cx = 3 * DW / 4, cy = DH / 2;
        bool any_diff = false;
        for (int dy = -20; dy <= 20; dy++)
            for (int dx = 0; dx <= 20; dx++)
                if (dpx(left_cx + dx, cy + dy) != dpx(right_cx - dx, cy + dy)) any_diff = true;
        CHECK(any_diff);
    }
}
```

Call all 6 functions in `main()` (before the `if (failures)` line). Note: the `DW/DH/dbuf/dpx/dclear` block already exists at lines 139–143 but sits AFTER its first use — put the new tests at the end of the file (after the decor tests), nothing needs to move.

- [x] **Step 2: Run to see it fail**

Run: `cd test && make test_robot_eyes && ./test_robot_eyes`
Expected: compile FAIL — `ROBOT_EMOTION_BORED` undeclared.

- [x] **Step 3: Implement**

`robot_eyes.h` — replace the current enum with the 28-value enum (exact order from spec section 1, with `ROBOT_EMOTION_COUNT`), update the doc comment (the blink constants are NEUTRAL's schedule; other emotions have their own), and add after `robot_eyes_is_closed`:

```c
// Per-emotion blink schedule (interval/duration from the EMOTIONS table in
// the .c). robot_eyes_is_closed(t) is this with ROBOT_EMOTION_NEUTRAL.
bool robot_eyes_is_closed_for(robot_emotion_t emotion, uint32_t now_ms);
```

`robot_eyes.c` — extend the structs + table (put the table BEFORE the two blink functions since blink reads it):

```c
typedef struct {
    int height_pct, width_pct, corner_pct, y_shift_pct, brow_slant_pct;
    int bottom_cut_pct;   // % of eye height erased from the bottom (0 = full squircle)
} eye_params_t;

typedef enum { ROBOT_MOTION_NONE, ROBOT_MOTION_SHAKE, ROBOT_MOTION_BOUNCE,
               ROBOT_MOTION_OSCILLATE } robot_motion_t;
typedef enum { ROBOT_DROP_NONE, ROBOT_DROP_SWEAT, ROBOT_DROP_TEAR } robot_drop_t;

typedef struct {
    eye_params_t left, right;
    uint32_t blink_interval_ms, blink_duration_ms;
    robot_motion_t motion;
    int motion_amp_pct;   // SHAKE/BOUNCE: % of r; OSCILLATE: ± on height_pct
    robot_drop_t drop;
} emotion_params_t;
```

The 28-entry table — exact values from spec section 4 (see the committed `robot_eyes.c` for the full listing; the NEUTRAL entry uses the two blink constants).

Blink (replaces the old function, placed after the table):

```c
bool robot_eyes_is_closed_for(robot_emotion_t emotion, uint32_t now_ms) {
    if ((unsigned)emotion >= ROBOT_EMOTION_COUNT) emotion = ROBOT_EMOTION_NEUTRAL;
    const emotion_params_t *em = &EMOTIONS[emotion];
    return (now_ms % em->blink_interval_ms) < em->blink_duration_ms;
}

bool robot_eyes_is_closed(uint32_t now_ms) {
    return robot_eyes_is_closed_for(ROBOT_EMOTION_NEUTRAL, now_ms);
}
```

In `robot_eyes_render`: guard `if ((unsigned)emotion >= ROBOT_EMOTION_COUNT) emotion = ROBOT_EMOTION_NEUTRAL;` at the top, and change `bool closed = robot_eyes_is_closed(now_ms);` to `robot_eyes_is_closed_for(emotion, now_ms)`. Add a table comment: "KEEP IN SYNC with tools/emotions-preview.html EMOTIONS table".

- [x] **Step 4: Run tests → pass** — `cd test && make test_robot_eyes && ./test_robot_eyes` → `ALL PASS` (old + new tests).
- [x] **Step 5: Commit** — `git add -A && git commit -m "feat(robot_eyes): expand to 28 emotion states with per-emotion blink"`

---

### Task 2: Bottom-cut — the arc-eye family (happy/laughing/glee)

**Files:** Modify `components/robot_eyes/robot_eyes.c`; Test `test/test_robot_eyes.c`

**Interfaces:**
- Consumes: the `bottom_cut_pct` field already populated in the table (HAPPY 45, LAUGHING 55, GLEE 60).
- Produces: `render_eye_box(...)` gains an `int bottom_cut_pct` parameter (static, file-internal).

- [x] **Step 1: Failing test**

```c
static void test_happy_family_is_top_heavy_arcs(void) {
    // bottom_cut erases the eye's lower part → EYE pixels above the panel
    // center must strongly outnumber those below. NEUTRAL (no cut) is the
    // control: roughly balanced.
    robot_emotion_t arcs[] = { ROBOT_EMOTION_HAPPY, ROBOT_EMOTION_LAUGHING, ROBOT_EMOTION_GLEE };
    for (unsigned k = 0; k < 3; k++) {
        dclear();
        robot_eyes_render(dbuf, DW, DH, DH, 0, 500, arcs[k], EYE, BG);
        int above = 0, below = 0, cy = DH / 2;
        for (int y = 0; y < DH; y++)
            for (int x = 0; x < DW / 2; x++) {
                if (dpx(x, y) != EYE) continue;
                if (y < cy) above++; else below++;
            }
        CHECK(above > 2 * below);
    }
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 500, ROBOT_EMOTION_NEUTRAL, EYE, BG);
    int above = 0, below = 0, cy = DH / 2;
    for (int y = 0; y < DH; y++)
        for (int x = 0; x < DW / 2; x++) {
            if (dpx(x, y) != EYE) continue;
            if (y < cy) above++; else below++;
        }
    CHECK(above <= 2 * below);
}
```

- [x] **Step 2: Run → FAIL** (`above > 2*below` is false since nothing is cut yet).
- [x] **Step 3: Implement** — `render_eye_box` gains an `int bottom_cut_pct` param; after filling the eye and BEFORE the brow wedge:

```c
    if (bottom_cut_pct > 0) {
        int cut_h = (eh * bottom_cut_pct) / 100;
        if (cut_h > 0) {
            // Erase the eye's bottom AND the glow beneath it so the open
            // arc reads cleanly against the background.
            int cut_y = ey + eh - cut_h;
            gfx_fill_rect(buf, buf_w, buf_rows, gx, cut_y, gw, (gy + gh) - cut_y, bg_color);
        }
    }
```

The call site in `robot_eyes_render` passes `p->bottom_cut_pct`. The closed-eye (blink band) branch is unchanged, no cut.

- [x] **Step 4: Run → ALL PASS.**
- [x] **Step 5: Commit** — `feat(robot_eyes): arc-shaped eyes via bottom cut for happy family`

---

### Task 3: Motion — SHAKE / BOUNCE / OSCILLATE

**Files:** Modify `components/robot_eyes/robot_eyes.c`; Test `test/test_robot_eyes.c`

**Interfaces:**
- Produces: `static void motion_offsets(const emotion_params_t*, int r, uint32_t now_ms, int *dx, int *dy, int *dh_pct)`.

- [x] **Step 1: Failing test**

```c
static bool frames_differ(robot_emotion_t e, uint32_t t1, uint32_t t2) {
    static uint16_t a[DW * DH], b[DW * DH];
    robot_eyes_render(a, DW, DH, DH, 0, t1, e, EYE, BG);
    robot_eyes_render(b, DW, DH, DH, 0, t2, e, EYE, BG);
    for (int i = 0; i < DW * DH; i++) if (a[i] != b[i]) return true;
    return false;
}

static void test_motion_emotions_animate_while_open(void) {
    // Both instants sit outside the respective emotion's blink window.
    CHECK(frames_differ(ROBOT_EMOTION_FRUSTRATED, 150, 210));  // SHAKE: 60ms half-cycle
    CHECK(frames_differ(ROBOT_EMOTION_CONFUSED,   150, 210));  // SHAKE
    // BOUNCE: 150 vs 300, NOT 150 vs 450 — the triangle wave is symmetric
    // around its 300ms peak, so 150 and 450 quantize to the same offset.
    CHECK(frames_differ(ROBOT_EMOTION_GLEE,       150, 300));
    CHECK(frames_differ(ROBOT_EMOTION_LAUGHING,   150, 275));  // OSCILLATE: phases in 250ms cycle
    CHECK(!frames_differ(ROBOT_EMOTION_NEUTRAL,   500, 560));  // control: no motion
}
```

- [x] **Step 2: Run → FAIL.**
- [x] **Step 3: Implement** — add before `robot_eyes_render`:

```c
// Motion is a pure function of now_ms (square/triangle waves) — no RNG,
// preserving this file's deterministic contract. dx/dy are pixel offsets;
// dh_pct adds to height_pct before scaling.
static void motion_offsets(const emotion_params_t *em, int r, uint32_t now_ms,
                           int *dx, int *dy, int *dh_pct) {
    *dx = 0; *dy = 0; *dh_pct = 0;
    int amp = em->motion_amp_pct;
    switch (em->motion) {
    case ROBOT_MOTION_SHAKE: {          // horizontal jitter, 120ms square wave
        int a = (r * amp) / 100;
        if (a < 1) a = 1;
        *dx = ((now_ms / 60u) % 2u) ? a : -a;
        break;
    }
    case ROBOT_MOTION_BOUNCE: {         // vertical hop 0→-amp→0, 600ms triangle
        uint32_t ph = now_ms % 600u;
        uint32_t tri = ph < 300u ? ph : 600u - ph;
        *dy = -(int)(((uint32_t)((r * amp) / 100) * tri) / 300u);
        break;
    }
    case ROBOT_MOTION_OSCILLATE: {      // height_pct wobble ±amp, 250ms triangle
        uint32_t ph = now_ms % 250u;
        uint32_t tri = ph < 125u ? ph : 250u - ph;
        *dh_pct = (int)((2u * (uint32_t)amp * tri) / 125u) - amp;
        break;
    }
    default: break;
    }
}
```

In `robot_eyes_render`, after fetching `em`: call `motion_offsets(em, r, now_ms, &mdx, &mdy, &mdh)`; in the eye loop use `int eh = (base_eye_h * (p->height_pct + mdh)) / 100;`, center `cy = cy0 + (r * p->y_shift_pct) / 100 + mdy`, `ex = cxs[i] + mdx - ew / 2`. The closed branch uses the offset `ex`/`cy` too.

- [x] **Step 4: Run → ALL PASS** (Task 1's canary band test re-verifies motion stays in band).
- [x] **Step 5: Commit** — `feat(robot_eyes): shake/bounce/oscillate motion modifiers`

---

### Task 4: Drops — SWEAT / TEAR (drawn in-band)

**Files:** Modify `components/robot_eyes/robot_eyes.c`; Test `test/test_robot_eyes.c`

**Interfaces:**
- Produces: `static void render_drop(uint16_t *buf, int buf_w, int buf_rows, robot_drop_t drop, int ex, int ey, int ew, int eh, int r, int band_bottom_y, uint32_t now_ms, uint16_t color)` — called with the RIGHT eye's geometry (per the reference image, the drop always sits at the right eye).

- [x] **Step 1: Failing test**

```c
static int count_eye_px(void) {
    int n = 0;
    for (int i = 0; i < DW * DH; i++) if (dbuf[i] == EYE) n++;
    return n;
}

static void test_sweat_drop_pulses(void) {
    // NERVOUS is open at both 150 and 650 (blink 2200/120); 150%800<500 →
    // drop visible, 650%800>=500 → hidden. The eyes themselves are
    // identical at both instants, so the pixel delta IS the sweat drop.
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 150, ROBOT_EMOTION_NERVOUS, EYE, BG);
    int with_drop = count_eye_px();
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 650, ROBOT_EMOTION_NERVOUS, EYE, BG);
    int without_drop = count_eye_px();
    CHECK(with_drop > without_drop);
}

static void test_tear_slides_down_over_time(void) {
    // CRYING is open at 300 and 900 (blink 4500/250). Different tear
    // phases → different frames, and the later phase must reach DEEPER
    // (lowest lit row sits lower).
    CHECK(frames_differ(ROBOT_EMOTION_CRYING, 300, 900));
    int lowest_a = -1, lowest_b = -1;
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 300, ROBOT_EMOTION_CRYING, EYE, BG);
    for (int y = 0; y < DH; y++) for (int x = 0; x < DW; x++) if (dpx(x, y) == EYE) lowest_a = y;
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 900, ROBOT_EMOTION_CRYING, EYE, BG);
    for (int y = 0; y < DH; y++) for (int x = 0; x < DW; x++) if (dpx(x, y) == EYE) lowest_b = y;
    CHECK(lowest_b > lowest_a);
}
```

- [x] **Step 2: Run → FAIL.**
- [x] **Step 3: Implement**

```c
// Water drop: a filled circle with a pointed triangular tail above it.
// SWEAT pulses on/off on an 800ms cycle at the right eye's top-outer
// corner; TEAR slides from just under the right eye down to the dirty
// band's bottom edge on a 1200ms sawtooth, then resets. Drawn inside the
// eyes band — no new decor band, no extra SPI cost.
#define ROBOT_SWEAT_CYCLE_MS   800u
#define ROBOT_SWEAT_VISIBLE_MS 500u
#define ROBOT_TEAR_CYCLE_MS   1200u

static void render_drop(uint16_t *buf, int buf_w, int buf_rows, robot_drop_t drop,
                        int ex, int ey, int ew, int eh, int r, int band_bottom_y,
                        uint32_t now_ms, uint16_t color) {
    int dr = r / 8;
    if (dr < 1) dr = 1;
    int cx, cy;
    if (drop == ROBOT_DROP_SWEAT) {
        if ((now_ms % ROBOT_SWEAT_CYCLE_MS) >= ROBOT_SWEAT_VISIBLE_MS) return;
        cx = ex + ew;
        cy = ey - dr;
    } else if (drop == ROBOT_DROP_TEAR) {
        int start_y = ey + eh + 2 * dr;
        int end_y = band_bottom_y - dr - 1;
        if (end_y < start_y) end_y = start_y;
        uint32_t ph = now_ms % ROBOT_TEAR_CYCLE_MS;
        cy = start_y + (int)(((uint32_t)(end_y - start_y) * ph) / ROBOT_TEAR_CYCLE_MS);
        cx = ex + (3 * ew) / 4;
    } else {
        return;
    }
    gfx_fill_circle(buf, buf_w, buf_rows, cx, cy, dr, color);
    gfx_fill_triangle(buf, buf_w, buf_rows, cx, cy - 3 * dr,
                       cx - dr, cy, cx + dr, cy, color);
}
```

In `robot_eyes_render`: the eye loop stashes `ex/ey/ew/eh` for i==1 (the right eye, motion offsets included); after the loop:

```c
    // Drops anchor on the right eye's OPEN geometry (computed above whether
    // or not this frame is mid-blink) so they don't jump during a blink.
    if (em->drop != ROBOT_DROP_NONE) {
        int band_bottom = cy0 + (r * ROBOT_EYES_MAX_REACH_PCT) / 100;
        render_drop(buf, buf_w, buf_rows, em->drop, right_ex, right_ey,
                     right_ew, right_eh, r, band_bottom, now_ms, eye_color);
    }
```

- [x] **Step 4: Run → ALL PASS** (the canary test verifies drops stay in band, including t=5999 with the tear at its deepest).
- [x] **Step 5: Commit** — `feat(robot_eyes): sweat/tear drops for nervous, anxious, crying`

---

### Task 5: Decor mapping — LISTENING→WAVES, LAUGHING→MOUTH

**Files:** Modify `components/robot_eyes/robot_eyes.c` (`robot_eyes_decor_for`); Test `test/test_robot_eyes.c`

- [x] **Step 1: Failing test** — extend `test_decor_for_maps_happy_sleepy_and_surprised_only` (renamed to `test_decor_for_mapping`):

```c
static void test_decor_for_mapping(void) {
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_HAPPY) == ROBOT_DECOR_MOUTH);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_LAUGHING) == ROBOT_DECOR_MOUTH);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SLEEPY) == ROBOT_DECOR_ZZZ);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SURPRISED) == ROBOT_DECOR_WAVES);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_LISTENING) == ROBOT_DECOR_WAVES);
    // representatives of the no-decor emotions
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_NEUTRAL) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SAD) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_ANGRY) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_CONFUSED) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SUSPICIOUS) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_GLEE) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_CRYING) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SQUINT) == ROBOT_DECOR_NONE);
}
```

- [x] **Step 2: Run → FAIL** (LISTENING/LAUGHING return NONE).
- [x] **Step 3: Implement** — add 2 cases to the switch:

```c
    case ROBOT_EMOTION_LAUGHING:  return ROBOT_DECOR_MOUTH;
    case ROBOT_EMOTION_LISTENING: return ROBOT_DECOR_WAVES;
```

- [x] **Step 4: Run → ALL PASS**, then the whole suite: `cd test && make test` → every binary PASS.
- [x] **Step 5: Commit** — `feat(robot_eyes): decor mapping for listening and laughing`

---

### Task 6: HTML preview — `tools/emotions-preview.html`

**Files:** Create `tools/emotions-preview.html`

**Interfaces:**
- Consumes: the EMOTIONS table + geometry/blink/motion/drop formulas from `robot_eyes.c` (faithful JS port, marked `// KEEP IN SYNC with components/robot_eyes/robot_eyes.c EMOTIONS[]`).

**File content requirements (one self-contained file, no CDNs):**
1. JS port: `EMOTIONS` (28 entries, exact values from the C table), `isClosed`, `motionOffsets`, `renderEyes(ctx, w, h, nowMs, emotion, eyeColor, bgColor)` — same formulas: `r = panel_h/4`, slot width budget, glow = per-channel-halved color, brow wedge, bottom cut, drop. Port all 3 decors (mouth/zzz/waves) with the same band percentages.
2. A grid of 28 cells (7 groups per the spec, each cell a 240×216 canvas CSS-scaled), labels with name + tag (`asym`, `shake`, `sweat`, `40w`, `state`) like the reference image, all animating from one shared `requestAnimationFrame` loop.
3. Click a cell → a zoom panel at the top runs that emotion.
4. Controls: eye color picker (default cyan `#00e5ff`), panel preset select `240×216 (ST7789)` / `128×52 (SSD1306)`.

- [x] **Step 1: Write the file** (no automated test — dev tool; verified visually and via a node smoke test that exercises `renderEyes` for all 28 emotions × 10 times × 2 panel sizes with a mock canvas context).
- [x] **Step 2: Verify** — open in a browser (`open tools/emotions-preview.html`): all 28 cells present, asym cells asymmetric, shake/bounce/tear/sweat moving, blink rates visibly different (BORED slow vs ANXIOUS fast), happy family renders as open-bottomed arcs.
- [x] **Step 3: Commit** — `feat(tools): HTML preview for 28-state emotion system`

---

## Self-Review

- **Spec coverage:** enum (T1), table+blink (T1), bottom cut (T2), motion (T3), drops (T4), decor (T5), band invariant (T1 canary, re-run by every later task), HTML preview (T6), the spec's 6 test groups (T1: 1,2,3,4,5; T5: 6). `main.c` untouched ✓.
- **Type consistency:** `robot_eyes_is_closed_for(robot_emotion_t, uint32_t)` used consistently in T1/T3/T4; `frames_differ` defined in T3 and reused by T4 (which runs after) ✓.
- **Placeholders:** no TBD/TODO left; every code step shows the code.

## Execution Notes (post-completion)

- All 6 tasks executed and merged to main (fast-forward) at `b7edc69`.
- One deviation: Task 3's original GLEE bounce check compared t=150 vs t=450 —
  two instants symmetric around the triangle wave's peak that quantize to the
  same pixel offset. Fixed the test to compare 150 vs 300 (a genuine phase
  difference); the implementation was correct.
