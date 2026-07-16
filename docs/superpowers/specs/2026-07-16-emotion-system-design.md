# Emotion System — 28-state robot eyes animation

**Date:** 2026-07-16
**Status:** Implemented (merged to main)

## Goal

Expand the `robot_eyes` component from 8 to **28 emotion states** (7 emotion
groups) per the user's reference image + spec, plus an HTML preview page for
reviewing the animations in a browser before flashing the board.

Principles carried over from the existing code:

- **Parametric, not bitmaps** — each emotion is a parameter set on top of one
  shared shape engine (squircle + glow + wedge), not a per-emotion frame.
- **Pure function of `now_ms`** — no RNG, no mutable state; the same `now_ms`
  always yields the same frame. Host-testable, no hardware access.
- **Backward compatible** — the 8 existing enum values keep their names and
  order; `main.c` needs no changes in this round.

## Non-goals (later rounds)

- Mapping the 28 new states into the FSM / MCP tools / `main.c` (existing call
  sites keep working).
- Emotion-to-emotion transitions/tweening (currently a hard cut; unchanged).

## 1. Enum

Keep the first 8 values unchanged, append 20 new values **after** them (never
in between):

```c
typedef enum {
    // existing — order and values unchanged
    ROBOT_EMOTION_NEUTRAL, ROBOT_EMOTION_HAPPY, ROBOT_EMOTION_SAD,
    ROBOT_EMOTION_SURPRISED, ROBOT_EMOTION_ANGRY, ROBOT_EMOTION_SLEEPY,
    ROBOT_EMOTION_CONFUSED, ROBOT_EMOTION_SUSPICIOUS,
    // group 1 — neutral & attention
    ROBOT_EMOTION_LISTENING, ROBOT_EMOTION_PONDERING, ROBOT_EMOTION_FOCUSED,
    // group 2 — positive
    ROBOT_EMOTION_LAUGHING, ROBOT_EMOTION_GLEE, ROBOT_EMOTION_AWE,
    // group 3 — negative
    ROBOT_EMOTION_CRYING,
    // group 4 — angry / frustration
    ROBOT_EMOTION_FURIOUS, ROBOT_EMOTION_FRUSTRATED,
    ROBOT_EMOTION_ANNOYED, ROBOT_EMOTION_UNIMPRESSED,
    // group 5 — fear & anxiety
    ROBOT_EMOTION_WORRIED, ROBOT_EMOTION_NERVOUS, ROBOT_EMOTION_ANXIOUS,
    ROBOT_EMOTION_SCARED, ROBOT_EMOTION_SHOCKED,
    // group 6 — tired / low energy
    ROBOT_EMOTION_TIRED, ROBOT_EMOTION_BORED,
    // group 7 — suspicion / judgement
    ROBOT_EMOTION_SKEPTICAL, ROBOT_EMOTION_SQUINT,
    ROBOT_EMOTION_COUNT,   // = 28
} robot_emotion_t;
```

## 2. Per-eye parameters (extended `eye_params_t`)

Existing fields: `height_pct, width_pct, corner_pct, y_shift_pct, brow_slant_pct`.

New field:

- **`bottom_cut_pct`** (0–100): after drawing the squircle + glow, cover the
  bottom `bottom_cut_pct%` of the eye's height with background → produces the
  "open-bottomed arc" shape for the happy/laughing/glee family that the
  current squircle cannot express. 0 = no cut (all pre-existing emotions).
  The cut also covers the glow in that region so the arc reads cleanly on a
  black background.

## 3. Per-emotion parameters (extended `emotion_params_t`)

- **`blink_interval_ms`, `blink_duration_ms`** — a blink schedule per emotion.
  New API `bool robot_eyes_is_closed_for(robot_emotion_t e, uint32_t now_ms)`.
  The old `robot_eyes_is_closed(now_ms)` stays, equivalent to calling the new
  function with NEUTRAL (NEUTRAL keeps exactly 3000/150 via the two existing
  constants — old tests don't break).
- **`motion`** — `ROBOT_MOTION_NONE / SHAKE / BOUNCE / OSCILLATE` plus
  `motion_amp_pct`:
  - `SHAKE`: **x** offset as a square wave ±amp (amp = % of `r`), 120 ms
    period. Used by FRUSTRATED, CONFUSED.
  - `BOUNCE`: **y** offset as a triangle wave 0→−amp→0, 600 ms period. Used
    by GLEE.
  - `OSCILLATE`: modulates `height_pct` ±amp% as a triangle wave, 250 ms
    period (LAUGHING's "fast wobble" / the sheet's `40w` tag).
  - All motion is a pure function of `now_ms` (triangle/square waves), no RNG.
- **`drop`** — `ROBOT_DROP_NONE / SWEAT / TEAR`, drawn **inside the eyes
  band** (no new decor band, no extra SPI cost):
  - `SWEAT`: a small drop (circle + pointed wedge above it, via
    `gfx_fill_circle` + `gfx_fill_triangle`) at the **right** eye's top-outer
    corner (per the reference image), pulsing on/off on a ~800 ms cycle.
    Used by NERVOUS, ANXIOUS.
  - `TEAR`: a drop sliding from just under the right eye downward on a
    ~1200 ms sawtooth, then resetting (a static tear looks stranger than a
    moving one). Used by CRYING.

## 4. The 28-emotion table

Values are this project's own creative choices (tuned by eye via the HTML
preview), following the spec's descriptions; the left/right columns differ
only for the `asym` emotions. Notation: `H/W/C/Y/B/Cut` =
height/width/corner/y_shift/brow_slant/bottom_cut (pct).

| Emotion | Left H/W/C/Y/B/Cut | Right (if different) | Blink (ms) | Motion | Drop | Decor |
|---|---|---|---|---|---|---|
| NEUTRAL | 100/100/100/0/0/0 | — | 3000/150 | — | — | — |
| LISTENING | 105/100/100/0/0/0 | — | 5000/120 | — | — | WAVES |
| PONDERING | 80/95/100/0/0/0 | — | 4000/250 | — | — | — |
| FOCUSED | 45/100/60/0/0/0 | — | 8000/150 | — | — | — |
| HAPPY | 75/105/140/−5/0/45 | — | 3000/150 | — | — | MOUTH |
| LAUGHING | 60/105/150/−8/0/55 | — | 3000/150 | OSC 15 | — | MOUTH |
| GLEE | 50/105/160/−10/0/60 | — | 3000/150 | BOUNCE 8 | — | — |
| AWE | 130/100/200/−5/0/0 | — | 1600/100 | — | — | — |
| SAD | 85/90/70/20/−40/0 | — | 3500/200 | — | — | — |
| CRYING | 80/88/70/24/−45/0 | — | 4500/250 | — | TEAR | — |
| ANGRY | 55/95/25/0/45/0 | — | 3000/150 | — | — | — |
| FURIOUS | 45/95/20/4/60/0 | — | 2500/120 | — | — | — |
| FRUSTRATED | 55/95/25/2/40/0 | — | 3000/150 | SHAKE 6 | — | — |
| ANNOYED | 45/95/30/6/25/0 | 60/95/30/0/15/0 | 3800/200 | — | — | — |
| UNIMPRESSED | 30/100/40/10/0/0 | 40/100/40/2/0/0 | 4500/250 | — | — | — |
| WORRIED | 75/95/60/6/−35/0 | — | 3000/150 | — | — | — |
| NERVOUS | 70/95/55/6/−35/0 | — | 2200/120 | — | SWEAT | — |
| ANXIOUS | 80/95/55/4/−40/0 | — | 1400/100 | — | SWEAT | — |
| SCARED | 130/105/80/−8/0/0 | — | 1800/100 | — | — | — |
| SHOCKED | 135/108/110/−10/0/0 | — | 6000/100 | — | — | — |
| SURPRISED | 130/105/100/−15/0/0 | — | 3000/150 | — | — | WAVES* |
| SLEEPY | 30/90/50/25/0/0 | — | 3000/300 | — | — | ZZZ |
| TIRED | 35/92/45/18/−20/0 | — | 3500/300 | — | — | — |
| BORED | 25/100/40/12/0/0 | — | 7000/400 | — | — | — |
| SKEPTICAL | 100/100/100/−5/0/0 | 35/100/60/8/0/0 | 4000/200 | — | — | — |
| SUSPICIOUS | 50/100/35/6/25/0 | 30/100/45/10/0/0 | 4000/200 | — | — | — |
| SQUINT | 18/100/30/0/0/0 | — | 9000/150 | — | — | — |
| CONFUSED | 100/100/100/−15/−30/0 | 100/100/100/0/0/0 | 3000/150 | SHAKE 5 | — | — |

\* SURPRISED keeps the WAVES decor for compatibility with the current
`main.c` (which uses SURPRISED as its "listening" state); once the FSM
switches to LISTENING in a later round, consider dropping WAVES from
SURPRISED.

SUSPICIOUS changed from symmetric to asym per the reference image (retuning
the old values is legitimate — the table is a creative choice, not an API).

## 5. Dirty-band constraint (the most important invariant)

`ROBOT_EYES_MAX_REACH_PCT = 140` **stays unchanged** — the decor bands start
at 1.45r, so the eyes band must not exceed 1.4r. Every table entry must
satisfy:

```
0.8 * height_pct(+osc amp if OSCILLATE) + |y_shift_pct| + bounce_amp + 20 (glow) ≤ 140
```

Worst cases in the table above: SHOCKED = 0.8·135 + 10 + 20 = 138 ✓;
SURPRISED = 104 + 15 + 20 = 139 ✓ (same as today);
LAUGHING (osc) = 0.8·(60+15) + 8 + 20 = 88 ✓; GLEE (bounce) = 40+10+8+20 = 78 ✓.
SHAKE only offsets horizontally and doesn't affect the vertical band. The
SWEAT/TEAR drops must stay inside the band (checked by test). **A new test
enforces this formula for all 28 entries** — an emotion exceeding the limit
fails the test instead of silently clipping on screen.

## 6. Decor

The decor system (`MOUTH/ZZZ/WAVES`) keeps its separate-band mechanism.
`robot_eyes_decor_for` is extended per the Decor column of the table above.
SWEAT/TEAR are **not** decor — they are drawn in-band.

## 7. HTML preview — `tools/emotions-preview.html`

- A single self-contained file (no CDNs), 2D canvas, faithful JS port of the
  C logic: same parameter table, same geometry/blink/motion/drop formulas.
- A grid of 28 cells all animating simultaneously (layout mirroring the
  reference image, with name labels + `asym/shake/sweat/state` tags), click a
  cell to zoom it, controls for eye color / panel aspect (240×240 ST7789 vs
  128×64 SSD1306).
- The JS table is hand-copied from the C table, marked with a
  `// KEEP IN SYNC with robot_eyes.c EMOTIONS[]` comment on both sides.
  Manual sync is acceptable (generating both tables from one source is
  over-engineering for a dev tool).

## 8. Testing (host, extending `test/test_robot_eyes.c`)

1. The table has all `ROBOT_EMOTION_COUNT` entries, none of them all-zero.
2. Band fit: render all 28 emotions at multiple `now_ms` values (covering
   blink, shake, bounce, osc, tear-slide phases) into a buffer sized exactly
   to the dirty band plus canary rows above/below — the canaries must never
   be painted.
3. Blink: `robot_eyes_is_closed(t) == robot_eyes_is_closed_for(NEUTRAL, t)`;
   spot-check a few per-emotion schedules (BORED slow, ANXIOUS fast, SQUINT
   rare) for correct interval/duration.
4. Determinism: two renders of the same `(emotion, now_ms)` produce identical
   buffers.
5. Asym: SKEPTICAL/SUSPICIOUS/ANNOYED/UNIMPRESSED render different left/right
   halves; mirrored emotions stay mirror-symmetric (drop/wedge aside).
6. New decor mapping (LISTENING→WAVES) plus the old mapping unchanged.

## 9. What does NOT change

- The signatures of `robot_eyes_render`, `robot_eyes_dirty_band`, and the
  entire decor API.
- The two old blink constants (they become the default/NEUTRAL schedule).
- `main.c`, the FSM, MCP tools.
