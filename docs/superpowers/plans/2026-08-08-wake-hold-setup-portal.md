# Wake-Hold Setup Portal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Holding the Wake button for 10 continuous seconds reboots the device straight into the WiFi/gateway setup portal, without needing a USB cable or `esptool` NVS erase.

**Architecture:** A pure hold/release decision function (host-testable) drives a new tick counter in the existing button-debounce state machine, which fires two new `button_id_t` events (`BTN_WAKE_RELEASE`, `BTN_WAKE_HOLD`). `main.c` defers the normal Wake-tap toggle from press-down to `BTN_WAKE_RELEASE` (barge-in stays instant on press-down, unchanged), and `BTN_WAKE_HOLD` sets a one-shot NVS flag and reboots. `app_main()` checks that flag right after `wifi_sta_start()` and jumps straight to the existing `provisioning_start()` if set — the same "reboot into a clean state" pattern `do_repair()` already uses.

**Tech Stack:** ESP-IDF v5.4 (C11), FreeRTOS, host-side `cc`-built unit tests (`test/Makefile`, no ESP-IDF dependency for pure-logic files).

## Global Constraints

- `board_types.h`'s `button_id_t`: "The first three values must stay 0/1/2 (main.c relies on them); new buttons are only ever appended." New values go after `BTN_EMOTION`, never inserted earlier in the enum.
- `on_button()` runs in the button task's context (small 3072-word stack) and must never touch display/audio hardware directly — only flip flags, adjust ints, and call queue-based helpers like `status_push()`/`show_volume_overlay()`.
- Barge-in (`BTN_WAKE` press-down while `APP_SPEAKING`) must stay zero-added-latency — do not defer or gate it behind any new logic.
- `provisioning_start()` assumes `esp_netif_init()`/`esp_event_loop_create_default()`/`esp_wifi_init()` have already run — only call it from `app_main()` after `wifi_sta_start()`, never from the button task.
- Design doc: `docs/superpowers/specs/2026-08-08-wake-hold-setup-portal-design.md` — read it for the full rationale; this plan implements it verbatim.

---

## Task 1: Pure hold/release decision logic + host test

**Files:**
- Create: `components/buttons/include/button_hold_logic.h`
- Create: `components/buttons/button_hold_logic.c`
- Modify: `components/buttons/CMakeLists.txt`
- Create: `test/test_button_hold.c`
- Modify: `test/Makefile`

**Interfaces:**
- Produces: `btn_hold_event_t` enum (`BTN_HOLD_EVENT_NONE`, `BTN_HOLD_EVENT_RELEASE`, `BTN_HOLD_EVENT_HOLD`) and `btn_hold_event_t btn_hold_step(bool released, int *held_ticks, bool *hold_fired)`, plus `BTN_HOLD_THRESHOLD_TICKS` — all consumed by Task 3's `gpio_buttons.c` changes.

- [ ] **Step 1: Write the header**

Create `components/buttons/include/button_hold_logic.h`:

```c
#pragma once
#include <stdbool.h>

// Pure, host-testable hold/release timing for a single button — no GPIO or
// RTOS access. The button driver's 20ms debounce scan tick is the unit of
// time here; a caller polling at a different rate must scale the threshold.
#define BTN_HOLD_TICK_MS 20
#define BTN_HOLD_THRESHOLD_TICKS (10000 / BTN_HOLD_TICK_MS)  // 10s

typedef enum {
    BTN_HOLD_EVENT_NONE,     // nothing to report this tick
    BTN_HOLD_EVENT_RELEASE,  // released before reaching the hold threshold
    BTN_HOLD_EVENT_HOLD,     // just crossed the threshold, still held
} btn_hold_event_t;

// Call once per scan tick while the button is confirmed held (i.e. every
// tick between debounce-confirm and the release that ends the press).
// `held_ticks`/`hold_fired` are the caller-owned persistent state for THIS
// button, reset to (0, false) by the caller when a new press begins.
//
// released: true if the pin reads "not pressed" on this tick.
//
// Fires BTN_HOLD_EVENT_HOLD exactly once per press, the first tick the
// threshold is reached. After that, further ticks (still held) return
// NONE, and the eventual release also returns NONE — a press that became
// a hold never additionally reports a release.
btn_hold_event_t btn_hold_step(bool released, int *held_ticks, bool *hold_fired);
```

- [ ] **Step 2: Write the failing test**

Create `test/test_button_hold.c`:

```c
#include "button_hold_logic.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// Simulates `n_ticks` ticks of continuous hold (released=false), returning
// the last non-NONE event seen (or NONE if none fired).
static btn_hold_event_t hold_for(int n_ticks, int *held_ticks, bool *hold_fired) {
    btn_hold_event_t last = BTN_HOLD_EVENT_NONE;
    for (int i = 0; i < n_ticks; i++) {
        btn_hold_event_t ev = btn_hold_step(false, held_ticks, hold_fired);
        if (ev != BTN_HOLD_EVENT_NONE) last = ev;
    }
    return last;
}

static void test_short_tap_releases_before_threshold(void) {
    int ticks = 0; bool fired = false;
    btn_hold_event_t during = hold_for(5, &ticks, &fired);  // 100ms held
    CHECK(during == BTN_HOLD_EVENT_NONE);
    CHECK(fired == false);
    btn_hold_event_t on_release = btn_hold_step(/*released=*/true, &ticks, &fired);
    CHECK(on_release == BTN_HOLD_EVENT_RELEASE);
}

static void test_hold_fires_exactly_at_threshold(void) {
    int ticks = 0; bool fired = false;
    // One tick short of the threshold: nothing yet.
    btn_hold_event_t during = hold_for(BTN_HOLD_THRESHOLD_TICKS - 1, &ticks, &fired);
    CHECK(during == BTN_HOLD_EVENT_NONE);
    CHECK(fired == false);
    // The threshold-crossing tick: fires HOLD exactly once.
    btn_hold_event_t at_threshold = btn_hold_step(false, &ticks, &fired);
    CHECK(at_threshold == BTN_HOLD_EVENT_HOLD);
    CHECK(fired == true);
}

static void test_hold_past_threshold_does_not_refire(void) {
    int ticks = 0; bool fired = false;
    hold_for(BTN_HOLD_THRESHOLD_TICKS, &ticks, &fired);  // crosses threshold once
    CHECK(fired == true);
    // Keep holding well past the threshold: no repeat HOLD events.
    btn_hold_event_t still_held = hold_for(100, &ticks, &fired);
    CHECK(still_held == BTN_HOLD_EVENT_NONE);
}

static void test_release_after_hold_fired_reports_nothing(void) {
    int ticks = 0; bool fired = false;
    hold_for(BTN_HOLD_THRESHOLD_TICKS, &ticks, &fired);  // HOLD already fired
    btn_hold_event_t on_release = btn_hold_step(/*released=*/true, &ticks, &fired);
    CHECK(on_release == BTN_HOLD_EVENT_NONE);
}

int main(void) {
    test_short_tap_releases_before_threshold();
    test_hold_fires_exactly_at_threshold();
    test_hold_past_threshold_does_not_refire();
    test_release_after_hold_fired_reports_nothing();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
```

- [ ] **Step 3: Add the Makefile target and run it to confirm the test fails to build**

Edit `test/Makefile`. Add near the other `*_CFLAGS`/`SRC_*` pairs (after the `I2S_PCM_CFLAGS`/`SRC_I2S_PCM` block):

```makefile
BUTTON_HOLD_CFLAGS = -std=c11 -Wall -Wextra -g -O0 -I../components/buttons/include
SRC_BUTTON_HOLD = ../components/buttons/button_hold_logic.c
```

In the `test:` target, add `test_button_hold` to the prerequisite list and add `./test_button_hold` to the run lines:

```makefile
test: test_provisioning_ssid test_provisioning_form test_display_font test_lugo_protocol test_mcp_server test_board_select test_board_facades test_gfx test_robot_eyes test_statusbar test_battery_logic test_pairing_logic test_i2s_pcm test_button_hold
	./test_provisioning_ssid
	./test_provisioning_form
	./test_display_font
	./test_lugo_protocol
	./test_mcp_server
	./test_board_select
	./test_board_facades
	./test_gfx
	./test_robot_eyes
	./test_statusbar
	./test_battery_logic
	./test_pairing_logic
	./test_i2s_pcm
	./test_button_hold
	@echo '--- mcp frame size ---'
	python3 ../tools/check_mcp_frame_size.py
```

Add the build rule next to `test_i2s_pcm`'s:

```makefile
test_button_hold: test_button_hold.c $(SRC_BUTTON_HOLD)
	$(CC) $(BUTTON_HOLD_CFLAGS) -o $@ $^
```

Add cleanup to the `clean:` target's file list — the current last line has no
trailing backslash (it ends the command), so it needs one added:

```makefile
	       test_pairing_logic test_pairing_logic.dSYM \
	       test_i2s_pcm test_i2s_pcm.dSYM \
	       test_button_hold test_button_hold.dSYM
```

Run: `cd test && make test_button_hold`
Expected: FAIL — `button_hold_logic.c` doesn't exist yet (`No such file or directory` from the compiler/linker).

- [ ] **Step 4: Write the implementation**

Create `components/buttons/button_hold_logic.c`:

```c
#include "button_hold_logic.h"

btn_hold_event_t btn_hold_step(bool released, int *held_ticks, bool *hold_fired) {
    if (released) {
        return (*hold_fired) ? BTN_HOLD_EVENT_NONE : BTN_HOLD_EVENT_RELEASE;
    }
    if (*hold_fired) return BTN_HOLD_EVENT_NONE;  // already fired; wait for release
    (*held_ticks)++;
    if (*held_ticks >= BTN_HOLD_THRESHOLD_TICKS) {
        *hold_fired = true;
        return BTN_HOLD_EVENT_HOLD;
    }
    return BTN_HOLD_EVENT_NONE;
}
```

- [ ] **Step 5: Wire it into the buttons component build**

Edit `components/buttons/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "buttons.c" "drivers/gpio_buttons.c" "button_hold_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES driver board)
```

- [ ] **Step 6: Run the host test and confirm it passes**

Run: `cd test && make test_button_hold && ./test_button_hold`
Expected: `ALL PASS`

- [ ] **Step 7: Run the full host test suite to confirm nothing else broke**

Run: `cd test && make test`
Expected: every existing test still prints its pass line, `test_button_hold` prints `ALL PASS`, `check_mcp_frame_size.py` succeeds.

- [ ] **Step 8: Commit**

```bash
git add components/buttons/include/button_hold_logic.h components/buttons/button_hold_logic.c \
        components/buttons/CMakeLists.txt test/test_button_hold.c test/Makefile
git commit -m "$(cat <<'EOF'
feat(buttons): pure hold/release decision logic for a 10s Wake hold

Host-testable, no GPIO access -- exercises the exact tick math the
gpio_buttons.c driver will drive in the next commit.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: One-shot "show setup portal on next boot" NVS flag

**Files:**
- Modify: `components/wifi/include/wifi_cfg.h`
- Modify: `components/wifi/wifi_cfg.c`

**Interfaces:**
- Consumes: nothing new (uses the same NVS namespace `"aa_cfg"` `wifi_cfg_load`/`wifi_cfg_save` already use).
- Produces: `esp_err_t wifi_cfg_request_setup(void)` and `bool wifi_cfg_take_setup_request(void)`, consumed by Task 4's `main.c` changes.

This component has no host test today (`wifi_cfg.c` calls real `nvs_*` APIs and the host harness has no NVS shim — same reason `wifi_cfg_load`/`wifi_cfg_save` aren't host-tested either). Verification here is a firmware build; end-to-end behavior is verified on hardware in Task 4.

- [ ] **Step 1: Add the declarations**

Edit `components/wifi/include/wifi_cfg.h`, append after the existing `wifi_cfg_save` declaration:

```c
// Requests the setup portal be shown on next boot, regardless of what's
// saved in ssid/password/server_host/server_port. One-shot: cleared the
// next time wifi_cfg_take_setup_request() is called. Call this before
// esp_restart() — it does not restart on its own.
esp_err_t wifi_cfg_request_setup(void);

// Returns true (and clears the flag) if wifi_cfg_request_setup() was
// called before the last reboot. Call once at boot, before deciding
// whether to skip straight to the setup portal. Returns false, not an
// error, if the flag was never set or the "aa_cfg" namespace doesn't
// exist yet.
bool wifi_cfg_take_setup_request(void);
```

- [ ] **Step 2: Implement them**

Edit `components/wifi/wifi_cfg.c`, append at the end of the file:

```c
#define FORCE_SETUP_KEY "force_setup"

esp_err_t wifi_cfg_request_setup(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(h, FORCE_SETUP_KEY, 1);
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    return err;
}

bool wifi_cfg_take_setup_request(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return false;  // namespace never created
    if (err != ESP_OK) return false;

    uint8_t flag = 0;
    err = nvs_get_u8(h, FORCE_SETUP_KEY, &flag);
    if (err != ESP_OK || flag == 0) {
        nvs_close(h);
        return false;
    }

    nvs_erase_key(h, FORCE_SETUP_KEY);
    nvs_commit(h);
    nvs_close(h);
    return true;
}
```

- [ ] **Step 3: Build the firmware to confirm it compiles**

Run:
```bash
source ~/esp/esp-idf/export.sh
idf.py -B build-s3sm -DSDKCONFIG=sdkconfig.s3sm \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.s3supermini" \
       build
```
Expected: build succeeds (these two new functions aren't called from anywhere yet, so nothing else changes — this just confirms `wifi_cfg.c`/`.h` compile cleanly).

- [ ] **Step 4: Commit**

```bash
git add components/wifi/include/wifi_cfg.h components/wifi/wifi_cfg.c
git commit -m "$(cat <<'EOF'
feat(wifi): one-shot NVS flag to force the setup portal on next boot

Same aa_cfg namespace wifi_cfg_load/save already use. Not called from
anywhere yet -- wired up in the next two commits.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: `BTN_WAKE_RELEASE` / `BTN_WAKE_HOLD` events in the button driver

**Files:**
- Modify: `components/board/include/board_types.h`
- Modify: `components/buttons/drivers/gpio_buttons.c`

**Interfaces:**
- Consumes: `btn_hold_event_t`, `btn_hold_step()`, `BTN_HOLD_THRESHOLD_TICKS` from Task 1's `button_hold_logic.h`.
- Produces: `button_id_t` gains `BTN_WAKE_RELEASE` and `BTN_WAKE_HOLD`, dispatched by the existing `on_press` callback the same way `BTN_WAKE`/`BTN_VOL_UP`/etc. already are. Consumed by Task 4's `main.c` changes.

No new host test in this task — `gpio_buttons.c` calls real `driver/gpio.h` functions and was never host-tested before this change either (the pure decision logic it now calls _is_ tested, in Task 1). Verification here is a firmware build; end-to-end behavior is verified on hardware in Task 4.

- [ ] **Step 1: Append the two new button IDs**

Edit `components/board/include/board_types.h`. Current enum:

```c
typedef enum {
    BTN_WAKE,      // wake / conversation toggle
    BTN_VOL_UP,
    BTN_VOL_DOWN,
    BTN_EMOTION,   // tact switch: show a random emotion on the eyes
} button_id_t;
```

Replace with (new values appended after `BTN_EMOTION`, per this file's own "new buttons are only ever appended" comment two lines above the enum):

```c
typedef enum {
    BTN_WAKE,      // wake / conversation toggle
    BTN_VOL_UP,
    BTN_VOL_DOWN,
    BTN_EMOTION,   // tact switch: show a random emotion on the eyes
    // Wake-only: fired by the button driver's hold-tracking (see
    // gpio_buttons.c). A press that releases before the 10s hold threshold
    // fires RELEASE; one that reaches the threshold fires HOLD instead
    // (and never also fires RELEASE for that same press).
    BTN_WAKE_RELEASE,
    BTN_WAKE_HOLD,
} button_id_t;
```

- [ ] **Step 2: Add hold-tracking state and wire `btn_hold_step()` into the scan loop**

Edit `components/buttons/drivers/gpio_buttons.c`. Current file:

```c
#include "buttons.h"
#include "buttons_gpio.h"
#include "board.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "buttons";

#define NBTN 4
static int s_gpios[NBTN];  // filled from board cfg in gpio_buttons_start; <0 = absent
static const button_id_t s_ids[NBTN]   = { BTN_WAKE, BTN_VOL_UP, BTN_VOL_DOWN, BTN_EMOTION };

static void (*s_cb)(button_id_t);

// Per-button debounce state machine, one 20ms scan tick for all buttons.
// Nothing here ever blocks the scan loop — the previous version busy-waited
// inside the loop for the pressed button's release, so HOLDING one button
// (e.g. Vol+) froze every other button, including Wake/barge-in.
typedef enum {
    BTN_ST_RELEASED,   // idle high (pull-up)
    BTN_ST_DEBOUNCE,   // saw a press edge; confirm it next tick (20ms settle)
    BTN_ST_HELD,       // fired; ignore until released (one event per press)
} btn_state_t;

static void buttons_task(void *arg) {
    (void)arg;
    btn_state_t st[NBTN];
    for (int i = 0; i < NBTN; i++) st[i] = BTN_ST_RELEASED;
    for (;;) {
        for (int i = 0; i < NBTN; i++) {
            if (s_gpios[i] < 0) continue;   // button not present on this board
            int lvl = gpio_get_level(s_gpios[i]);
            switch (st[i]) {
            case BTN_ST_RELEASED:
                if (lvl == 0) st[i] = BTN_ST_DEBOUNCE;   // press edge
                break;
            case BTN_ST_DEBOUNCE:
                if (lvl == 0) {                          // still low = real press
                    ESP_LOGI(TAG, "press gpio%d", s_gpios[i]);
                    if (s_cb) s_cb(s_ids[i]);
                    st[i] = BTN_ST_HELD;
                } else {
                    st[i] = BTN_ST_RELEASED;             // bounce — ignore
                }
                break;
            case BTN_ST_HELD:
                if (lvl == 1) st[i] = BTN_ST_RELEASED;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

Replace the whole file's content above `gpio_buttons_start` with:

```c
#include "buttons.h"
#include "buttons_gpio.h"
#include "board.h"
#include "button_hold_logic.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "buttons";

#define NBTN 4
static int s_gpios[NBTN];  // filled from board cfg in gpio_buttons_start; <0 = absent
static const button_id_t s_ids[NBTN]   = { BTN_WAKE, BTN_VOL_UP, BTN_VOL_DOWN, BTN_EMOTION };

static void (*s_cb)(button_id_t);

// Per-button debounce state machine, one 20ms scan tick for all buttons.
// Nothing here ever blocks the scan loop — the previous version busy-waited
// inside the loop for the pressed button's release, so HOLDING one button
// (e.g. Vol+) froze every other button, including Wake/barge-in.
typedef enum {
    BTN_ST_RELEASED,   // idle high (pull-up)
    BTN_ST_DEBOUNCE,   // saw a press edge; confirm it next tick (20ms settle)
    BTN_ST_HELD,       // fired; ignore until released (one event per press)
} btn_state_t;

// Hold-duration tracking, Wake-only (see btn_hold_step in button_hold_logic.h).
// Indexed in parallel with s_gpios/s_ids; unused (and untouched) for every
// button other than s_ids[i] == BTN_WAKE.
static int  s_held_ticks[NBTN];
static bool s_hold_fired[NBTN];

static void buttons_task(void *arg) {
    (void)arg;
    btn_state_t st[NBTN];
    for (int i = 0; i < NBTN; i++) st[i] = BTN_ST_RELEASED;
    for (;;) {
        for (int i = 0; i < NBTN; i++) {
            if (s_gpios[i] < 0) continue;   // button not present on this board
            int lvl = gpio_get_level(s_gpios[i]);
            switch (st[i]) {
            case BTN_ST_RELEASED:
                if (lvl == 0) st[i] = BTN_ST_DEBOUNCE;   // press edge
                break;
            case BTN_ST_DEBOUNCE:
                if (lvl == 0) {                          // still low = real press
                    ESP_LOGI(TAG, "press gpio%d", s_gpios[i]);
                    if (s_cb) s_cb(s_ids[i]);
                    st[i] = BTN_ST_HELD;
                    s_held_ticks[i] = 0;
                    s_hold_fired[i] = false;
                } else {
                    st[i] = BTN_ST_RELEASED;             // bounce — ignore
                }
                break;
            case BTN_ST_HELD:
                if (s_ids[i] == BTN_WAKE) {
                    btn_hold_event_t ev = btn_hold_step(lvl == 1, &s_held_ticks[i], &s_hold_fired[i]);
                    if (ev == BTN_HOLD_EVENT_RELEASE) {
                        if (s_cb) s_cb(BTN_WAKE_RELEASE);
                    } else if (ev == BTN_HOLD_EVENT_HOLD) {
                        ESP_LOGI(TAG, "wake held to threshold on gpio%d", s_gpios[i]);
                        if (s_cb) s_cb(BTN_WAKE_HOLD);
                    }
                }
                if (lvl == 1) st[i] = BTN_ST_RELEASED;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

(`gpio_buttons_start` and `buttons_gpio_ops` below stay exactly as they are — not shown again here.)

- [ ] **Step 3: Add the new component dependency**

`button_hold_logic.h` lives in `components/buttons/include`, the same `INCLUDE_DIRS` `gpio_buttons.c` already has — no `CMakeLists.txt` change needed here (Task 1 already added `button_hold_logic.c` to `SRCS`).

- [ ] **Step 4: Build the firmware**

Run:
```bash
source ~/esp/esp-idf/export.sh
idf.py -B build-s3sm -DSDKCONFIG=sdkconfig.s3sm \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.s3supermini" \
       build
```
Expected: build succeeds. `main.c`'s `on_button()` still has a `switch (id)` with no `default:` — check the build output for a `-Wswitch` warning/error about the two new unhandled enum values (`BTN_WAKE_RELEASE`, `BTN_WAKE_HOLD`). If the build fails or warns here, that's expected and confirms the enum change landed; Task 4 adds the missing cases.

- [ ] **Step 5: Commit**

```bash
git add components/board/include/board_types.h components/buttons/drivers/gpio_buttons.c
git commit -m "$(cat <<'EOF'
feat(buttons): fire BTN_WAKE_RELEASE/BTN_WAKE_HOLD from a 10s Wake hold

Uses button_hold_logic's pure tick math (previous commit). Every other
button's behavior is byte-for-byte unchanged. main.c doesn't handle the
two new events yet -- next commit.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Wire it into `main.c` — defer the Wake toggle, handle the hold, force the portal on boot

**Files:**
- Modify: `main/main.c`

**Interfaces:**
- Consumes: `BTN_WAKE_RELEASE`/`BTN_WAKE_HOLD` (Task 3), `wifi_cfg_request_setup()`/`wifi_cfg_take_setup_request()` (Task 2).
- Produces: end-user-visible behavior — this is the task that makes the feature actually do something.

No host test — `main.c` isn't part of the host test harness (it's the full application, wired to real hardware/RTOS throughout). Verified by firmware build + hardware smoke test on the `lugo-s3-supermini` board already flashed this session.

- [ ] **Step 1: Defer the Wake-tap toggle to release; add the two new cases**

Edit `main/main.c`. Current `on_button()` (see lines ~803-855):

```c
// Runs in the button task context — only flips flags, adjusts the volume int,
// queues display messages, and sends a WS control frame (network, not
// hardware). It must never call display/audio hardware directly.
static void on_button(button_id_t id) {
    switch (id) {
    case BTN_WAKE: {
        if (s_state == APP_SPEAKING) {
            // Barge-in. Switch to LISTENING NOW so on_audio drops any further
            // downlink frames and the mic reopens; ask spk_task to do the actual
            // audio flush/reset (I2S + opus) — the button task must not touch audio
            // hardware from its small stack. Then tell the server to cancel the turn.
            // Connection stays open; do NOT toggle to Idle.
            s_turn_ending = false;
            s_state = APP_LISTENING;
            s_active = true;
            s_audio_reset_req = true;   // spk_task flushes the queue + resets I2S/opus
            // Start dropping captured audio from the press itself: the speaker
            // is still playing the bot until spk_task services s_barge_in, and
            // the mic is already open (state is LISTENING as of the line above).
            s_mic_flush_req = true;
            s_last_activity_s = now_s();
            ws_client_send_abort("user");
            send_listening_status();
            break;
        }
        // Not speaking: toggle idle/listening. Waking re-enables the link (it may
        // be asleep after an idle goodbye); going idle puts it back to sleep.
        if (s_active) go_idle(); else go_active();
        break;
    }
    case BTN_VOL_UP:
    case BTN_VOL_DOWN: {
```

Replace the `case BTN_WAKE:` block (keep `case BTN_VOL_UP:` onward untouched) with:

```c
    case BTN_WAKE: {
        if (s_state == APP_SPEAKING) {
            // Barge-in. Switch to LISTENING NOW so on_audio drops any further
            // downlink frames and the mic reopens; ask spk_task to do the actual
            // audio flush/reset (I2S + opus) — the button task must not touch audio
            // hardware from its small stack. Then tell the server to cancel the turn.
            // Connection stays open; do NOT toggle to Idle.
            s_turn_ending = false;
            s_state = APP_LISTENING;
            s_active = true;
            s_audio_reset_req = true;   // spk_task flushes the queue + resets I2S/opus
            // Start dropping captured audio from the press itself: the speaker
            // is still playing the bot until spk_task services s_barge_in, and
            // the mic is already open (state is LISTENING as of the line above).
            s_mic_flush_req = true;
            s_last_activity_s = now_s();
            ws_client_send_abort("user");
            send_listening_status();
            s_wake_press_handled = true;
            break;
        }
        // Not speaking: don't toggle idle/active on press-down anymore — a
        // hold that turns into a 10s setup-portal request (BTN_WAKE_HOLD)
        // must never flip app state first. A normal tap releases in well
        // under 10s, so deferring the toggle to BTN_WAKE_RELEASE is
        // imperceptible for the common case.
        s_wake_press_handled = false;
        break;
    }
    case BTN_WAKE_RELEASE: {
        // No-op if this press already acted on press-down (barge-in above).
        // Waking re-enables the link (it may be asleep after an idle
        // goodbye); going idle puts it back to sleep.
        if (!s_wake_press_handled) {
            if (s_active) go_idle(); else go_active();
        }
        break;
    }
    case BTN_WAKE_HOLD: {
        ESP_LOGI(TAG, "wake held 10s -- entering setup mode");
        status_push(false, ROBOT_EMOTION_NEUTRAL, "Setup mode", "Restarting...");
        esp_err_t err = wifi_cfg_request_setup();
        if (err != ESP_OK) ESP_LOGW(TAG, "wifi_cfg_request_setup failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1200));  // let the HUD message render before reboot
        esp_restart();
        break;
    }
    case BTN_VOL_UP:
    case BTN_VOL_DOWN: {
```

- [ ] **Step 2: Declare the new press-handled flag**

Edit `main/main.c` at line 105, right after `s_active`'s declaration:

```c
// Conversation gate: false = idle (mic muted) after connect; the Wake button
// toggles it. mic_task only streams when s_active. Written only by the button
// callback, read by mic_task.
static volatile bool s_active = false;
```

Add immediately below it:

```c
// Conversation gate: false = idle (mic muted) after connect; the Wake button
// toggles it. mic_task only streams when s_active. Written only by the button
// callback, read by mic_task.
static volatile bool s_active = false;

// Set by BTN_WAKE's press-down case when it already acted (barge-in), so
// the paired BTN_WAKE_RELEASE knows not to also fire the idle/active
// toggle for the same press. Only ever touched from the button task, so
// no volatile/atomic needed (unlike s_active, which mic_task also reads).
static bool s_wake_press_handled;
```

- [ ] **Step 3: Force the setup portal at boot if requested**

Edit `main/main.c`, in `app_main()`. Current code (see lines ~1265-1270):

```c
    ESP_ERROR_CHECK(wifi_cfg_load(&s_cfg));

    // wifi_sta_start runs even with an empty SSID: provisioning_start needs a
    // started, STA-mode radio to scan for networks before it flips to AP mode.
    ESP_ERROR_CHECK(wifi_sta_start(s_cfg.ssid, s_cfg.password));
    if (s_cfg.ssid[0] == '\0') {
```

Insert a new check between the `wifi_sta_start` call and the `if (s_cfg.ssid[0] == '\0')` check:

```c
    ESP_ERROR_CHECK(wifi_cfg_load(&s_cfg));

    // wifi_sta_start runs even with an empty SSID: provisioning_start needs a
    // started, STA-mode radio to scan for networks before it flips to AP mode.
    ESP_ERROR_CHECK(wifi_sta_start(s_cfg.ssid, s_cfg.password));
    if (wifi_cfg_take_setup_request()) {
        // Wake was held 10s (see on_button's BTN_WAKE_HOLD case). Skip
        // straight to the portal — don't wait on a connection the user is
        // likely here specifically to change.
        ESP_LOGI(TAG, "setup requested via wake-hold, starting setup portal");
        display_show("Setup mode", "Starting setup AP...");
        provisioning_start(&s_cfg);  // does not return
    }
    if (s_cfg.ssid[0] == '\0') {
```

- [ ] **Step 4: Build the firmware**

Run:
```bash
source ~/esp/esp-idf/export.sh
idf.py -B build-s3sm -DSDKCONFIG=sdkconfig.s3sm \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.s3supermini" \
       build
```
Expected: build succeeds with no `-Wswitch` warnings (all six `button_id_t` values now have a `case` in `on_button()`).

- [ ] **Step 5: Flash and smoke-test on the S3 SuperMini**

Run:
```bash
source ~/esp/esp-idf/export.sh
idf.py -B build-s3sm -DSDKCONFIG=sdkconfig.s3sm \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.s3supermini" \
       -p /dev/tty.usbmodem101 flash
```

If `esptool` reports "no sync reply" / fails to connect, manually enter download mode first (hold BOOT, tap RESET, release RESET after ~1s, then release BOOT) and retry the same command — same as the earlier flash this session.

Then manually verify, reading the serial log either via `idf.py -B build-s3sm -p /dev/tty.usbmodem101 monitor` in an actual terminal (it needs a real TTY — it will fail with "Monitor requires standard input to be attached to TTY" if run through a non-interactive tool), or by toggling RTS and reading raw serial for a fixed window the way this session did earlier when `idf.py monitor` wasn't usable:

1. **Normal tap:** short-press Wake from idle. Confirm it still toggles idle/active with no perceptible delay (same as before this change).
2. **Barge-in:** while the device is speaking (mid-TTS), press Wake. Confirm the barge-in still feels instant.
3. **Hold-to-setup:** from idle, hold Wake continuously for 10+ seconds. Confirm:
   - The HUD shows "Setup mode" / "Restarting..." before the reboot.
   - The device reboots and comes up as the `Lugo-XXXX` softAP at `192.168.9.1` (log line `provisioning: provisioning AP 'Lugo-XXXX' up at 192.168.9.1`), skipping the normal WiFi-connect attempt.
   - The portal form at `http://192.168.9.1` is pre-filled with the same SSID/host/port the device had before.
4. **Recovery:** submit the portal form with the original values unchanged (or the values needed for this device). Confirm the device reboots and reconnects normally, resuming the conversational app (not stuck back in setup mode).

- [ ] **Step 6: Commit**

```bash
git add main/main.c
git commit -m "$(cat <<'EOF'
feat(main): hold Wake 10s to reboot into the setup portal

Defers the normal Wake-tap idle/active toggle from press-down to
BTN_WAKE_RELEASE so a hold that becomes a setup request never flips app
state first; barge-in stays instant on press-down, unchanged. Sets the
one-shot NVS flag from wifi_cfg_request_setup() and reboots;
app_main() checks it right after wifi_sta_start() and jumps straight to
provisioning_start() -- the config form comes up pre-filled with the
device's current WiFi/gateway settings, same as any other entry into it.

Verified on lugo-s3-supermini: normal tap and barge-in unaffected;
10s hold reboots into the Lugo-XXXX softAP portal; submitting the form
reconnects normally.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** `wifi_cfg_request_setup`/`take_setup_request` (Task 2) ✓, `button_id_t` additions appended per the file's own convention (Task 3) ✓, hold/release driver logic + pure host-tested decision function (Tasks 1+3) ✓, `on_button()` deferral + `BTN_WAKE_HOLD` handling (Task 4) ✓, `app_main()` forced-portal check (Task 4) ✓, edge cases from the spec (hold-during-speaking, double-fire prevention, NVS-write failure, unchanged-form resubmit) all addressed in Task 4's code/smoke test and Task 1's test cases ✓.
- **Placeholder scan:** none — every step has real code or an exact command.
- **Type consistency:** `btn_hold_event_t`/`btn_hold_step`/`BTN_HOLD_THRESHOLD_TICKS` (Task 1) match their use in Task 3 exactly; `BTN_WAKE_RELEASE`/`BTN_WAKE_HOLD` (Task 3) match their `case` labels in Task 4 exactly; `wifi_cfg_request_setup`/`wifi_cfg_take_setup_request` (Task 2) match their call sites in Task 4 exactly.
