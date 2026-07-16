# 28-State Emotion System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mở rộng component `robot_eyes` từ 8 lên 28 emotion state (per spec `docs/superpowers/specs/2026-07-16-emotion-system-design.md`) với blink/motion/drop per-emotion, kèm HTML preview.

**Architecture:** Parametric table mở rộng trên shape engine sẵn có (squircle + glow + wedge), thêm bottom-cut cho họ vòm, motion offset (shake/bounce/oscillate) và drop (sweat/tear) — tất cả là pure function của `now_ms`. Enum cũ giữ nguyên vị trí.

**Tech Stack:** C11 (ESP-IDF component, host-tested qua `test/Makefile`), HTML+Canvas thuần (không CDN).

## Global Constraints

- 8 giá trị enum hiện có giữ nguyên tên + thứ tự; thêm 20 giá trị mới phía sau; `ROBOT_EMOTION_COUNT == 28`.
- Không RNG, không mutable state — cùng `(emotion, now_ms)` → cùng frame.
- `ROBOT_EYES_MAX_REACH_PCT = 140` không đổi; mọi emotion (kể cả motion/drop) phải nằm trong dirty band — test canary enforce.
- `robot_eyes_is_closed(t)` ≡ `robot_eyes_is_closed_for(ROBOT_EMOTION_NEUTRAL, t)`; NEUTRAL dùng đúng 2 constant blink cũ (3000/150).
- Không sửa `main.c`, không đổi chữ ký `robot_eyes_render`/`robot_eyes_dirty_band`/API decor.
- Test chạy bằng: `cd test && make test_robot_eyes && ./test_robot_eyes` (expected: `ALL PASS`).
- Bảng tham số 28 entry lấy **đúng giá trị** từ spec mục 4.

---

### Task 1: Enum 28 state + bảng đầy đủ + blink per-emotion

**Files:**
- Modify: `components/robot_eyes/include/robot_eyes.h` (enum, khai báo `robot_eyes_is_closed_for`)
- Modify: `components/robot_eyes/robot_eyes.c` (struct mở rộng, bảng 28 entry, blink table-driven)
- Test: `test/test_robot_eyes.c`

**Interfaces:**
- Produces: `robot_emotion_t` 28 giá trị + `ROBOT_EMOTION_COUNT`; `bool robot_eyes_is_closed_for(robot_emotion_t, uint32_t now_ms)`; struct nội bộ `emotion_params_t` có các field `bottom_cut_pct` (per-eye), `blink_interval_ms`, `blink_duration_ms`, `motion`, `motion_amp_pct`, `drop` (Task 2–4 render chúng).

- [ ] **Step 1: Viết test fail** — thêm vào `test/test_robot_eyes.c`:

```c
static void test_is_closed_for_neutral_matches_legacy(void) {
    for (uint32_t t = 0; t < 6200; t += 37)
        CHECK(robot_eyes_is_closed(t) == robot_eyes_is_closed_for(ROBOT_EMOTION_NEUTRAL, t));
}

static void test_per_emotion_blink_schedules(void) {
    // BORED: 7000/400 — chậm, nhắm lâu
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 0) == true);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 399) == true);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 400) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 6999) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_BORED, 7000) == true);
    // ANXIOUS: 1400/100 — nhanh
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_ANXIOUS, 99) == true);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_ANXIOUS, 100) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_ANXIOUS, 1400) == true);
    // SQUINT: 9000/150 — gần như không chớp
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_SQUINT, 150) == false);
    CHECK(robot_eyes_is_closed_for(ROBOT_EMOTION_SQUINT, 8999) == false);
}

static void test_every_emotion_paints_something_when_open(void) {
    // Bắt entry rỗng/toàn 0 trong bảng: mọi emotion, tại một thời điểm mắt mở,
    // phải vẽ được ít nhất một pixel EYE.
    for (int e = 0; e < ROBOT_EMOTION_COUNT; e++) {
        uint32_t t = 500;   // ngoài blink window của mọi entry (duration max 400)
        dclear();
        robot_eyes_render(dbuf, DW, DH, DH, 0, t, (robot_emotion_t)e, EYE, BG);
        int lit = 0;
        for (int i = 0; i < DW * DH; i++) if (dbuf[i] == EYE) lit++;
        CHECK(lit > 0);
    }
}

// Danh sách thời điểm phủ các pha: blink mở/đóng nhiều lịch khác nhau, shake
// (chu kỳ 120ms), bounce (600ms), oscillate (250ms), sweat pulse (800ms),
// tear slide (1200ms, sâu nhất ~ph=1199 → t=5999).
static const uint32_t SAMPLE_TIMES[] = {
    0, 59, 60, 149, 150, 199, 250, 399, 500, 650, 899,
    1300, 2100, 3049, 4400, 5999, 7100, 8999
};
#define N_SAMPLE_TIMES (sizeof SAMPLE_TIMES / sizeof SAMPLE_TIMES[0])

static void test_all_emotions_stay_inside_dirty_band(void) {
    // Render full panel; mọi hàng NGOÀI dirty band phải thuần BG với mọi
    // emotion × mọi thời điểm mẫu — emotion nào (kể cả motion/drop các task
    // sau) tràn band sẽ fail ở đây thay vì clip lặng lẽ trên màn hình.
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

Gọi cả 6 hàm trong `main()` (thêm trước dòng `if (failures)`). Lưu ý: khối `DW/DH/dbuf/dpx/dclear` đã tồn tại ở dòng 139–143 nhưng nằm SAU chỗ dùng — đặt các test mới ở cuối file (sau các test decor), không cần di chuyển gì.

- [ ] **Step 2: Chạy để thấy fail**

Run: `cd test && make test_robot_eyes && ./test_robot_eyes`
Expected: compile FAIL — `ROBOT_EMOTION_BORED` undeclared.

- [ ] **Step 3: Implement**

`robot_eyes.h` — thay enum hiện tại bằng enum 28 giá trị (đúng thứ tự spec mục 1, có `ROBOT_EMOTION_COUNT`), cập nhật doc comment (blink constants là lịch của NEUTRAL; các emotion khác có lịch riêng), thêm sau `robot_eyes_is_closed`:

```c
// Per-emotion blink schedule (interval/duration từ bảng EMOTIONS trong .c).
// robot_eyes_is_closed(t) tương đương gọi hàm này với ROBOT_EMOTION_NEUTRAL.
bool robot_eyes_is_closed_for(robot_emotion_t emotion, uint32_t now_ms);
```

`robot_eyes.c` — mở rộng struct + bảng (đặt bảng TRƯỚC hai hàm blink vì blink đọc bảng):

```c
typedef struct {
    int height_pct, width_pct, corner_pct, y_shift_pct, brow_slant_pct;
    int bottom_cut_pct;   // % chiều cao mắt bị cắt khỏi đáy (0 = squircle nguyên)
} eye_params_t;

typedef enum { ROBOT_MOTION_NONE, ROBOT_MOTION_SHAKE, ROBOT_MOTION_BOUNCE,
               ROBOT_MOTION_OSCILLATE } robot_motion_t;
typedef enum { ROBOT_DROP_NONE, ROBOT_DROP_SWEAT, ROBOT_DROP_TEAR } robot_drop_t;

typedef struct {
    eye_params_t left, right;
    uint32_t blink_interval_ms, blink_duration_ms;
    robot_motion_t motion;
    int motion_amp_pct;   // SHAKE/BOUNCE: % của r; OSCILLATE: ± lên height_pct
    robot_drop_t drop;
} emotion_params_t;
```

Bảng 28 entry — giá trị đúng theo spec mục 4 (viết đủ, ví dụ vài dòng):

```c
static const emotion_params_t EMOTIONS[ROBOT_EMOTION_COUNT] = {
    //                            {  H,  W,  C,  Y,  B,Cut}
    [ROBOT_EMOTION_NEUTRAL]    = { {100,100,100,  0,  0, 0}, {100,100,100,  0,  0, 0},
                                   ROBOT_EYES_BLINK_INTERVAL_MS, ROBOT_EYES_BLINK_DURATION_MS,
                                   ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_HAPPY]      = { { 75,105,140, -5,  0,45}, { 75,105,140, -5,  0,45},
                                   3000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SAD]        = { { 85, 90, 70, 20,-40, 0}, { 85, 90, 70, 20,-40, 0},
                                   3500, 200, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SURPRISED]  = { {130,105,100,-15,  0, 0}, {130,105,100,-15,  0, 0},
                                   3000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_ANGRY]      = { { 55, 95, 25,  0, 45, 0}, { 55, 95, 25,  0, 45, 0},
                                   3000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SLEEPY]     = { { 30, 90, 50, 25,  0, 0}, { 30, 90, 50, 25,  0, 0},
                                   3000, 300, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_CONFUSED]   = { {100,100,100,-15,-30, 0}, {100,100,100,  0,  0, 0},
                                   3000, 150, ROBOT_MOTION_SHAKE, 5, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SUSPICIOUS] = { { 50,100, 35,  6, 25, 0}, { 30,100, 45, 10,  0, 0},
                                   4000, 200, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_LISTENING]  = { {105,100,100,  0,  0, 0}, {105,100,100,  0,  0, 0},
                                   5000, 120, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_PONDERING]  = { { 80, 95,100,  0,  0, 0}, { 80, 95,100,  0,  0, 0},
                                   4000, 250, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_FOCUSED]    = { { 45,100, 60,  0,  0, 0}, { 45,100, 60,  0,  0, 0},
                                   8000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_LAUGHING]   = { { 60,105,150, -8,  0,55}, { 60,105,150, -8,  0,55},
                                   3000, 150, ROBOT_MOTION_OSCILLATE, 15, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_GLEE]       = { { 50,105,160,-10,  0,60}, { 50,105,160,-10,  0,60},
                                   3000, 150, ROBOT_MOTION_BOUNCE, 8, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_AWE]        = { {130,100,200, -5,  0, 0}, {130,100,200, -5,  0, 0},
                                   1600, 100, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_CRYING]     = { { 80, 88, 70, 24,-45, 0}, { 80, 88, 70, 24,-45, 0},
                                   4500, 250, ROBOT_MOTION_NONE, 0, ROBOT_DROP_TEAR },
    [ROBOT_EMOTION_FURIOUS]    = { { 45, 95, 20,  4, 60, 0}, { 45, 95, 20,  4, 60, 0},
                                   2500, 120, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_FRUSTRATED] = { { 55, 95, 25,  2, 40, 0}, { 55, 95, 25,  2, 40, 0},
                                   3000, 150, ROBOT_MOTION_SHAKE, 6, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_ANNOYED]    = { { 45, 95, 30,  6, 25, 0}, { 60, 95, 30,  0, 15, 0},
                                   3800, 200, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_UNIMPRESSED]= { { 30,100, 40, 10,  0, 0}, { 40,100, 40,  2,  0, 0},
                                   4500, 250, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_WORRIED]    = { { 75, 95, 60,  6,-35, 0}, { 75, 95, 60,  6,-35, 0},
                                   3000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_NERVOUS]    = { { 70, 95, 55,  6,-35, 0}, { 70, 95, 55,  6,-35, 0},
                                   2200, 120, ROBOT_MOTION_NONE, 0, ROBOT_DROP_SWEAT },
    [ROBOT_EMOTION_ANXIOUS]    = { { 80, 95, 55,  4,-40, 0}, { 80, 95, 55,  4,-40, 0},
                                   1400, 100, ROBOT_MOTION_NONE, 0, ROBOT_DROP_SWEAT },
    [ROBOT_EMOTION_SCARED]     = { {130,105, 80, -8,  0, 0}, {130,105, 80, -8,  0, 0},
                                   1800, 100, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SHOCKED]    = { {135,108,110,-10,  0, 0}, {135,108,110,-10,  0, 0},
                                   6000, 100, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_TIRED]      = { { 35, 92, 45, 18,-20, 0}, { 35, 92, 45, 18,-20, 0},
                                   3500, 300, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_BORED]      = { { 25,100, 40, 12,  0, 0}, { 25,100, 40, 12,  0, 0},
                                   7000, 400, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SKEPTICAL]  = { {100,100,100, -5,  0, 0}, { 35,100, 60,  8,  0, 0},
                                   4000, 200, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
    [ROBOT_EMOTION_SQUINT]     = { { 18,100, 30,  0,  0, 0}, { 18,100, 30,  0,  0, 0},
                                   9000, 150, ROBOT_MOTION_NONE, 0, ROBOT_DROP_NONE },
};
```

Blink (thay hàm cũ, đặt sau bảng):

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

Trong `robot_eyes_render`: guard `if ((unsigned)emotion >= ROBOT_EMOTION_COUNT) emotion = ROBOT_EMOTION_NEUTRAL;` ngay đầu, và đổi `bool closed = robot_eyes_is_closed(now_ms);` thành `robot_eyes_is_closed_for(emotion, now_ms)`. Ghi chú comment table: "KEEP IN SYNC with tools/emotions-preview.html EMOTIONS table".

- [ ] **Step 4: Chạy test pass** — `cd test && make test_robot_eyes && ./test_robot_eyes` → `ALL PASS` (test cũ + mới).
- [ ] **Step 5: Commit** — `git add -A && git commit -m "feat(robot_eyes): expand to 28 emotion states with per-emotion blink"`

---

### Task 2: Bottom-cut — họ mắt vòm (happy/laughing/glee)

**Files:** Modify `components/robot_eyes/robot_eyes.c`; Test `test/test_robot_eyes.c`

**Interfaces:**
- Consumes: field `bottom_cut_pct` đã có giá trị trong bảng (HAPPY 45, LAUGHING 55, GLEE 60).
- Produces: `render_eye_box(...)` nhận thêm tham số `int bottom_cut_pct` (static, nội bộ file).

- [ ] **Step 1: Test fail**

```c
static void test_happy_family_is_top_heavy_arcs(void) {
    // bottom_cut cắt đáy mắt → phần EYE phía trên tâm mắt phải nhiều hơn hẳn
    // phía dưới. NEUTRAL (không cut) làm đối chứng: chênh lệch không đáng kể.
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

- [ ] **Step 2: Run → FAIL** (`above > 2*below` sai vì chưa cắt).
- [ ] **Step 3: Implement** — `render_eye_box` thêm param `int bottom_cut_pct`, sau khi vẽ eye và TRƯỚC brow wedge:

```c
    if (bottom_cut_pct > 0) {
        int cut_h = (eh * bottom_cut_pct) / 100;
        if (cut_h > 0) {
            // Phủ nền lên đáy mắt VÀ phần glow bên dưới nó để vòm đọc rõ.
            int cut_y = ey + eh - cut_h;
            gfx_fill_rect(buf, buf_w, buf_rows, gx, cut_y, gw, (gy + gh) - cut_y, bg_color);
        }
    }
```

Call site trong `robot_eyes_render` truyền `p->bottom_cut_pct`. Mắt nhắm (closed band) giữ nguyên, không cut.

- [ ] **Step 4: Run → ALL PASS.**
- [ ] **Step 5: Commit** — `feat(robot_eyes): arc-shaped eyes via bottom cut for happy family`

---

### Task 3: Motion — SHAKE / BOUNCE / OSCILLATE

**Files:** Modify `components/robot_eyes/robot_eyes.c`; Test `test/test_robot_eyes.c`

**Interfaces:**
- Produces: `static void motion_offsets(const emotion_params_t*, int r, uint32_t now_ms, int *dx, int *dy, int *dh_pct)`.

- [ ] **Step 1: Test fail**

```c
static bool frames_differ(robot_emotion_t e, uint32_t t1, uint32_t t2) {
    static uint16_t a[DW * DH], b[DW * DH];
    robot_eyes_render(a, DW, DH, DH, 0, t1, e, EYE, BG);
    robot_eyes_render(b, DW, DH, DH, 0, t2, e, EYE, BG);
    for (int i = 0; i < DW * DH; i++) if (a[i] != b[i]) return true;
    return false;
}

static void test_motion_emotions_animate_while_open(void) {
    // Cả hai thời điểm đều nằm ngoài blink window của emotion tương ứng.
    CHECK(frames_differ(ROBOT_EMOTION_FRUSTRATED, 150, 210));  // SHAKE: nửa chu kỳ 60ms
    CHECK(frames_differ(ROBOT_EMOTION_CONFUSED,   150, 210));  // SHAKE
    CHECK(frames_differ(ROBOT_EMOTION_GLEE,       150, 450));  // BOUNCE: pha khác trong 600ms
    CHECK(frames_differ(ROBOT_EMOTION_LAUGHING,   150, 275));  // OSCILLATE: pha khác trong 250ms
    CHECK(!frames_differ(ROBOT_EMOTION_NEUTRAL,   500, 560));  // đối chứng: không motion
}
```

- [ ] **Step 2: Run → FAIL.**
- [ ] **Step 3: Implement** — thêm trước `robot_eyes_render`:

```c
// Motion là hàm thuần của now_ms (sóng vuông/tam giác) — không RNG, giữ
// contract deterministic của file này.
static void motion_offsets(const emotion_params_t *em, int r, uint32_t now_ms,
                           int *dx, int *dy, int *dh_pct) {
    *dx = 0; *dy = 0; *dh_pct = 0;
    int amp = em->motion_amp_pct;
    switch (em->motion) {
    case ROBOT_MOTION_SHAKE: {          // rung ngang, sóng vuông chu kỳ 120ms
        int a = (r * amp) / 100;
        if (a < 1) a = 1;
        *dx = ((now_ms / 60u) % 2u) ? a : -a;
        break;
    }
    case ROBOT_MOTION_BOUNCE: {         // nảy dọc 0→-amp→0, tam giác 600ms
        uint32_t ph = now_ms % 600u;
        uint32_t tri = ph < 300u ? ph : 600u - ph;
        *dy = -(int)(((uint32_t)((r * amp) / 100) * tri) / 300u);
        break;
    }
    case ROBOT_MOTION_OSCILLATE: {      // co giãn height_pct ±amp, tam giác 250ms
        uint32_t ph = now_ms % 250u;
        uint32_t tri = ph < 125u ? ph : 250u - ph;
        *dh_pct = (int)((2u * (uint32_t)amp * tri) / 125u) - amp;
        break;
    }
    default: break;
    }
}
```

Trong `robot_eyes_render`, sau khi lấy `em`: gọi `motion_offsets(em, r, now_ms, &mdx, &mdy, &mdh)`; trong vòng lặp mắt dùng `int hp = p->height_pct + mdh; int eh = (base_eye_h * hp) / 100;`, tâm `cy = cy0 + (r * p->y_shift_pct) / 100 + mdy`, `ex = cxs[i] + mdx - ew / 2`. Nhánh closed cũng dùng `ex`/`cy` đã offset.

- [ ] **Step 4: Run → ALL PASS** (canary band test từ Task 1 tự re-verify motion không tràn band).
- [ ] **Step 5: Commit** — `feat(robot_eyes): shake/bounce/oscillate motion modifiers`

---

### Task 4: Drops — SWEAT / TEAR (vẽ in-band)

**Files:** Modify `components/robot_eyes/robot_eyes.c`; Test `test/test_robot_eyes.c`

**Interfaces:**
- Produces: `static void render_drop(uint16_t *buf, int buf_w, int buf_rows, robot_drop_t drop, int ex, int ey, int ew, int eh, int r, int band_bottom_y, uint32_t now_ms, uint16_t color)` — gọi với hình học mắt PHẢI (theo reference image, giọt luôn ở mắt phải).

- [ ] **Step 1: Test fail**

```c
static int count_eye_px(void) {
    int n = 0;
    for (int i = 0; i < DW * DH; i++) if (dbuf[i] == EYE) n++;
    return n;
}

static void test_sweat_drop_pulses(void) {
    // NERVOUS mở mắt ở cả 150 lẫn 650 (blink 2200/120); 150%800<500 → giọt
    // hiện, 650%800>=500 → giọt ẩn. Mắt giữ nguyên nên chênh lệch pixel
    // chính là giọt mồ hôi.
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 150, ROBOT_EMOTION_NERVOUS, EYE, BG);
    int with_drop = count_eye_px();
    dclear();
    robot_eyes_render(dbuf, DW, DH, DH, 0, 650, ROBOT_EMOTION_NERVOUS, EYE, BG);
    int without_drop = count_eye_px();
    CHECK(with_drop > without_drop);
}

static void test_tear_slides_down_over_time(void) {
    // CRYING mở mắt ở 300 và 900 (blink 4500/250). Pha tear khác nhau →
    // frame khác nhau; và ở pha sau, giọt phải xuất hiện SÂU hơn (hàng EYE
    // thấp nhất nằm thấp hơn).
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

- [ ] **Step 2: Run → FAIL.**
- [ ] **Step 3: Implement**

```c
// Giọt nước: circle + đuôi tam giác nhọn phía trên. SWEAT nhấp nháy theo
// chu kỳ 800ms ở góc ngoài-trên mắt phải; TEAR trượt từ mép dưới mắt xuống
// đáy dirty band theo răng cưa 1200ms rồi reset. Vẽ trong band mắt — không
// thêm decor band, không tốn thêm SPI.
#define ROBOT_SWEAT_CYCLE_MS   800u
#define ROBOT_SWEAT_VISIBLE_MS 500u
#define ROBOT_TEAR_CYCLE_MS   1200u

static void render_drop(uint16_t *buf, int buf_w, int buf_rows, robot_drop_t drop,
                        int ex, int ey, int ew, int eh, int r, int band_bottom_y,
                        uint32_t now_ms, uint16_t color) {
    int dr = r / 8;
    if (dr < 1) dr = 1;
    if (drop == ROBOT_DROP_SWEAT) {
        if ((now_ms % ROBOT_SWEAT_CYCLE_MS) >= ROBOT_SWEAT_VISIBLE_MS) return;
        int cx = ex + ew, cy = ey - dr;
        gfx_fill_circle(buf, buf_w, buf_rows, cx, cy, dr, color);
        gfx_fill_triangle(buf, buf_w, buf_rows, cx, cy - 3 * dr,
                           cx - dr, cy, cx + dr, cy, color);
    } else if (drop == ROBOT_DROP_TEAR) {
        int start_y = ey + eh + 2 * dr;
        int end_y = band_bottom_y - dr - 1;
        if (end_y < start_y) end_y = start_y;
        uint32_t ph = now_ms % ROBOT_TEAR_CYCLE_MS;
        int cy = start_y + (int)(((uint32_t)(end_y - start_y) * ph) / ROBOT_TEAR_CYCLE_MS);
        int cx = ex + (3 * ew) / 4;
        gfx_fill_circle(buf, buf_w, buf_rows, cx, cy, dr, color);
        gfx_fill_triangle(buf, buf_w, buf_rows, cx, cy - 3 * dr,
                           cx - dr, cy, cx + dr, cy, color);
    }
}
```

Trong `robot_eyes_render`: vòng lặp mắt lưu lại `ex/ey/ew/eh` của i==1 (mắt phải, đã gồm motion offset); sau vòng lặp:

```c
    if (em->drop != ROBOT_DROP_NONE) {
        int band_bottom = cy0 + (r * ROBOT_EYES_MAX_REACH_PCT) / 100;
        render_drop(buf, buf_w, buf_rows, em->drop, right_ex, right_ey,
                     right_ew, right_eh, r, band_bottom, now_ms, eye_color);
    }
```

Với mắt nhắm, dùng hình học mắt mở để neo giọt (ey/eh của box mở — tính trước nhánh if closed) để giọt không nhảy khi chớp mắt.

- [ ] **Step 4: Run → ALL PASS** (canary test verify giọt không tràn band, kể cả t=5999 tear sâu nhất).
- [ ] **Step 5: Commit** — `feat(robot_eyes): sweat/tear drops for nervous, anxious, crying`

---

### Task 5: Decor mapping — LISTENING→WAVES, LAUGHING→MOUTH

**Files:** Modify `components/robot_eyes/robot_eyes.c` (`robot_eyes_decor_for`); Test `test/test_robot_eyes.c`

- [ ] **Step 1: Test fail** — mở rộng `test_decor_for_maps_happy_sleepy_and_surprised_only` (đổi tên thành `test_decor_for_mapping`):

```c
static void test_decor_for_mapping(void) {
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_HAPPY) == ROBOT_DECOR_MOUTH);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_LAUGHING) == ROBOT_DECOR_MOUTH);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SLEEPY) == ROBOT_DECOR_ZZZ);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SURPRISED) == ROBOT_DECOR_WAVES);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_LISTENING) == ROBOT_DECOR_WAVES);
    // đại diện các emotion không decor
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_NEUTRAL) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_GLEE) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_CRYING) == ROBOT_DECOR_NONE);
    CHECK(robot_eyes_decor_for(ROBOT_EMOTION_SQUINT) == ROBOT_DECOR_NONE);
}
```

- [ ] **Step 2: Run → FAIL** (LISTENING/LAUGHING trả NONE).
- [ ] **Step 3: Implement** — thêm 2 case vào switch:

```c
    case ROBOT_EMOTION_LAUGHING:  return ROBOT_DECOR_MOUTH;
    case ROBOT_EMOTION_LISTENING: return ROBOT_DECOR_WAVES;
```

- [ ] **Step 4: Run → ALL PASS**, chạy cả bộ: `cd test && make test` → tất cả binary PASS.
- [ ] **Step 5: Commit** — `feat(robot_eyes): decor mapping for listening and laughing`

---

### Task 6: HTML preview — `tools/emotions-preview.html`

**Files:** Create `tools/emotions-preview.html`

**Interfaces:**
- Consumes: bảng EMOTIONS + công thức hình học/blink/motion/drop từ `robot_eyes.c` (port trung thực sang JS, đánh dấu `// KEEP IN SYNC with components/robot_eyes/robot_eyes.c EMOTIONS[]`).

**Yêu cầu nội dung file (một file tự chứa, không CDN):**
1. JS port: `EMOTIONS` (28 entry, đúng giá trị bảng C), `isClosedFor`, `motionOffsets`, `renderEyes(ctx, w, h, nowMs, emotion, eyeColor, bgColor)` — cùng công thức: `r = panel_h/4`, slot width budget, glow = màu giảm nửa kênh, brow wedge, bottom cut, drop. Port cả 3 decor (mouth/zzz/waves) với cùng band %.
2. Grid 28 cell (7 nhóm theo spec, mỗi cell canvas 240×216 scale CSS ~50%), nhãn tên + tag (`asym`, `shake`, `sweat`, `40w`, `state`) như reference image, animation chạy đồng thời bằng một `requestAnimationFrame` chung.
3. Click cell → panel zoom to phía trên (canvas 480×432) chạy emotion đó.
4. Controls: color picker màu mắt (default cyan `#00e5ff`), select panel preset `240×216 (ST7789)` / `128×52 (SSD1306)`.

- [ ] **Step 1: Viết file** (không có test tự động — dev tool; verify bằng mắt).
- [ ] **Step 2: Verify** — mở bằng browser (`open tools/emotions-preview.html`), kiểm: đủ 28 cell, các cell asym bất đối xứng, shake/bounce/tear/sweat chuyển động, blink rates khác nhau rõ rệt (BORED chậm vs ANXIOUS nhanh), happy family là vòm hở đáy.
- [ ] **Step 3: Commit** — `feat(tools): HTML preview for 28-state emotion system`

---

## Self-Review

- **Spec coverage:** enum (T1), bảng+blink (T1), bottom cut (T2), motion (T3), drop (T4), decor (T5), band invariant (T1 canary + re-run mỗi task), HTML preview (T6), test 6 nhóm của spec mục 8 (T1: 1,2,3,4,5; T5: 6). Không sửa main.c ✓.
- **Type consistency:** `robot_eyes_is_closed_for(robot_emotion_t, uint32_t)` dùng thống nhất T1/T3/T4; `frames_differ` định nghĩa ở T3, T4 dùng lại (T4 chạy sau T3) ✓.
- **Placeholder:** không còn TBD/TODO; mọi bước code đều có code.
