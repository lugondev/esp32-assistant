# Emotion System — 28-state robot eyes animation

**Date:** 2026-07-16
**Status:** Approved design, pending implementation plan

## Goal

Mở rộng component `robot_eyes` từ 8 emotion lên **28 emotion state** (7 nhóm cảm xúc)
theo reference image + spec của người dùng, kèm một trang HTML preview để duyệt
animation trên browser trước khi nạp board.

Nguyên tắc giữ nguyên từ code hiện tại:

- **Parametric, không bitmap** — mỗi emotion là một bộ tham số trên cùng một shape
  engine (squircle + glow + wedge), không phải frame ảnh riêng.
- **Pure function of `now_ms`** — không RNG, không mutable state; cùng `now_ms`
  luôn cho cùng frame. Host-testable, không đụng hardware.
- **Backward compatible** — 8 giá trị enum hiện có giữ nguyên tên và thứ tự;
  `main.c` không phải sửa trong đợt này.

## Non-goals (đợt sau)

- Map 28 state mới vào FSM / MCP tools / `main.c` (các call site cũ vẫn chạy).
- Transition/tween giữa hai emotion (hiện tại chuyển cắt cứng, giữ nguyên).

## 1. Enum

Giữ nguyên 8 giá trị đầu, thêm 20 giá trị mới **phía sau** (không chen giữa):

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

## 2. Tham số per-eye (mở rộng `eye_params_t`)

Trường hiện có: `height_pct, width_pct, corner_pct, y_shift_pct, brow_slant_pct`.

Trường mới:

- **`bottom_cut_pct`** (0–100): sau khi vẽ squircle + glow, phủ một dải nền lên
  phần đáy mắt cao `bottom_cut_pct%` chiều cao mắt → tạo hình "vòm cong hở đáy"
  cho họ happy/laughing/glee. 0 = không cắt (mọi emotion cũ). Dải cắt phủ luôn
  cả glow trong vùng đó để vòm đọc rõ trên nền đen.

## 3. Tham số per-emotion (struct `emotion_params_t` mở rộng)

- **`blink_interval_ms`, `blink_duration_ms`** — lịch chớp mắt riêng từng emotion.
  API mới `bool robot_eyes_is_closed_for(robot_emotion_t e, uint32_t now_ms)`.
  Hàm cũ `robot_eyes_is_closed(now_ms)` giữ nguyên, tương đương gọi cho NEUTRAL
  (NEUTRAL giữ đúng 3000/150 như hai constant hiện có — test cũ không vỡ).
- **`motion`** — `ROBOT_MOTION_NONE / SHAKE / BOUNCE / OSCILLATE` + `motion_amp_pct`:
  - `SHAKE`: offset **x** dạng sóng vuông ±amp (amp = % của `r`), chu kỳ 120 ms.
    Dùng cho FRUSTRATED, CONFUSED.
  - `BOUNCE`: offset **y** sóng tam giác 0→−amp→0, chu kỳ 600 ms. Dùng cho GLEE.
  - `OSCILLATE`: modulate `height_pct` ±amp% sóng tam giác, chu kỳ 250 ms
    ("dao động nhanh" của LAUGHING).
  - Mọi motion đều là hàm thuần của `now_ms` (triangle/square wave), không RNG.
- **`drop`** — `ROBOT_DROP_NONE / SWEAT / TEAR`, vẽ **bên trong band mắt**
  (không thêm decor band mới, không tốn thêm SPI):
  - `SWEAT`: giọt nhỏ (circle + wedge nhọn phía trên, `gfx_fill_circle` +
    `gfx_fill_triangle`) ở góc ngoài-trên mắt **phải** (theo reference image),
    nhấp nháy hiện/ẩn chu kỳ ~800 ms. Dùng cho NERVOUS, ANXIOUS.
  - `TEAR`: giọt trượt từ mép dưới mắt phải xuống theo sóng răng cưa chu kỳ
    ~1200 ms rồi reset (đứng yên là lạ hơn chuyển động). Dùng cho CRYING.

## 4. Bảng 28 emotion

Giá trị là lựa chọn sáng tạo của project (tinh chỉnh bằng mắt qua HTML preview),
bám mô tả spec; cột trái/phải chỉ khác nhau ở các emotion `asym`. Ký hiệu:
`H/W/C/Y/B/Cut` = height/width/corner/y_shift/brow_slant/bottom_cut (pct).

| Emotion | Trái H/W/C/Y/B/Cut | Phải (nếu khác) | Blink (ms) | Motion | Drop | Decor |
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

\* SURPRISED giữ decor WAVES để tương thích với `main.c` hiện tại (đang dùng
SURPRISED làm trạng thái "đang nghe"); khi FSM chuyển sang LISTENING ở đợt sau,
cân nhắc bỏ WAVES khỏi SURPRISED.

SUSPICIOUS đổi từ đối xứng sang asym theo reference image (tinh chỉnh giá trị
cũ là hợp lệ — bảng là creative choice, không phải API).

## 5. Ràng buộc dirty band (invariant quan trọng nhất)

`ROBOT_EYES_MAX_REACH_PCT = 140` **giữ nguyên** — decor band bắt đầu tại 1.45r
nên band mắt không được vượt 1.4r. Mọi entry trong bảng phải thỏa:

```
0.8 * height_pct(+osc amp nếu OSCILLATE) + |y_shift_pct| + bounce_amp + 20 (glow) ≤ 140
```

Worst case bảng trên: SHOCKED = 0.8·135 + 10 + 20 = 138 ✓;
SURPRISED = 104 + 15 + 20 = 139 ✓ (như hiện tại);
LAUGHING (osc) = 0.8·(60+15) + 8 + 20 = 88 ✓; GLEE (bounce) = 40+10+8+20 = 78 ✓.
SHAKE chỉ offset ngang, không ảnh hưởng band dọc. Giọt SWEAT/TEAR phải nằm
trong band (kiểm bằng test). **Test mới enforce công thức này cho cả 28 entry**
— thêm emotion vượt ngưỡng sẽ fail test thay vì clip lặng lẽ trên màn hình.

## 6. Decor

Hệ decor (`MOUTH/ZZZ/WAVES`) giữ nguyên cơ chế band riêng. `robot_eyes_decor_for`
mở rộng theo cột Decor ở bảng trên. SWEAT/TEAR **không** phải decor — vẽ in-band.

## 7. HTML preview — `tools/emotions-preview.html`

- Một file tự chứa (không CDN), canvas 2D, port trung thực logic C sang JS:
  cùng bảng tham số, cùng công thức hình học/blink/motion/drop.
- Grid 28 ô chạy animation đồng thời (bố cục giống reference image, kèm nhãn
  tên + tag `asym/shake/sweat/state`), click một ô để phóng to, control chỉnh
  màu mắt / tỉ lệ panel (240×240 ST7789 vs 128×64 SSD1306).
- Bảng JS chép tay từ bảng C, đánh dấu comment `// KEEP IN SYNC with
  robot_eyes.c EMOTIONS[]` ở cả hai phía. Chấp nhận sync thủ công (một nguồn
  sinh hai bảng là over-engineering cho một dev tool).

## 8. Testing (host, `test/test_robot_eyes.c` mở rộng)

1. Bảng đủ `ROBOT_EMOTION_COUNT` entry, không entry nào toàn 0.
2. Band-fit: render cả 28 emotion tại nhiều `now_ms` (phủ đủ pha blink, shake,
   bounce, osc, tear-slide) vào buffer đúng bằng dirty band + 1 hàng canary
   trên/dưới — canary không bao giờ bị vẽ.
3. Blink: `robot_eyes_is_closed(t) == robot_eyes_is_closed_for(NEUTRAL, t)`;
   lịch riêng của vài emotion (BORED chậm, ANXIOUS nhanh, SQUINT hiếm) đúng
   interval/duration.
4. Determinism: hai lần render cùng `(emotion, now_ms)` cho buffer giống hệt.
5. Asym: SKEPTICAL/SUSPICIOUS/ANNOYED/UNIMPRESSED render hai nửa trái/phải
   khác nhau; các emotion mirror thì đối xứng gương (trừ drop/wedge).
6. Decor mapping mới (LISTENING→WAVES) và cũ không đổi.

## 9. Những gì KHÔNG đổi

- Chữ ký `robot_eyes_render`, `robot_eyes_dirty_band`, toàn bộ API decor.
- Hai constant blink cũ (thành default/NEUTRAL).
- `main.c`, FSM, MCP tools.
